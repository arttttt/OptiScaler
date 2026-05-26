#include "pch.h"
#include "IFeature_Dx9wDx11.h"

#include <Config.h>
#include <shaders/sharpen/Sharpen_Dx11.h>

namespace
{
    DXGI_FORMAT MapD3D9FormatToDxgi(D3DFORMAT fmt)
    {
        switch (fmt)
        {
        case D3DFMT_A8R8G8B8:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case D3DFMT_X8R8G8B8:
            return DXGI_FORMAT_B8G8R8X8_UNORM;
        case D3DFMT_A8B8G8R8:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DFMT_A2B10G10R10:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case D3DFMT_A16B16G16R16F:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:
            return DXGI_FORMAT_UNKNOWN;
        }
    }
}

IFeature_Dx9wDx11::IFeature_Dx9wDx11() = default;

IFeature_Dx9wDx11::~IFeature_Dx9wDx11()
{
    ReleaseAll();
}

bool IFeature_Dx9wDx11::Init(IDirect3DDevice9* gameDevice, IDirect3DDevice9Ex* gameDeviceEx, UINT width, UINT height,
                             D3DFORMAT format)
{
    if (gameDevice == nullptr || width == 0 || height == 0)
    {
        LOG_ERROR("Dx9wDx11: invalid Init args");
        return false;
    }

    if (gameDeviceEx == nullptr)
    {
        // The non-Ex CPU-readback path is a Phase 5b problem. Bail loudly so
        // the wrapper can log it and proceed with vanilla Present.
        LOG_WARN("Dx9wDx11: non-Ex device — bridge disabled (Phase 5b adds the CPU path)");
        return false;
    }

    _gameDevice = gameDevice;
    _gameDeviceEx = gameDeviceEx;
    _width = width;
    _height = height;
    _gameFormat = format;

    if (!CreateDx11Device())
        return false;

    if (!CreateSharedColor())
    {
        ReleaseAll();
        return false;
    }

    LOG_INFO("Dx9wDx11 Init: about to CreateQuery(EVENT)");
    const HRESULT qhr = _gameDevice->CreateQuery(D3DQUERYTYPE_EVENT, &_eventQuery);
    if (FAILED(qhr) || _eventQuery == nullptr)
    {
        LOG_ERROR("Dx9wDx11: CreateQuery(EVENT) failed hr=0x{:08X}", static_cast<uint32_t>(qhr));
        ReleaseAll();
        return false;
    }
    LOG_INFO("Dx9wDx11 Init: CreateQuery ok");

    // 5b RTV: skipped pending diagnosis. Previously this block crashed HL2
    // hard, killing the process between CreateSharedColor and the next log
    // emit. Going back to pure 5a behavior (CopyResource round-trip) until
    // we can identify the failure point with the breadcrumbs above.
    _sharedOutRtv = nullptr;

    _init = true;
    LOG_INFO("Dx9wDx11 bridge initialised ({}x{}, fmt=0x{:X}, rtv=none-by-design)", _width, _height,
             static_cast<uint32_t>(_gameFormat));
    return true;
}

bool IFeature_Dx9wDx11::CreateDx11Device()
{
    // Default adapter. Phase 5b LUID-matches the game's adapter for multi-GPU
    // boxes; for the single-GPU smoke test the default is correct.
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;

    // No D3D11_CREATE_DEVICE_DEBUG: the layer's break-on-error policy was
    // a candidate for the CreateRenderTargetView crash, so we err on the
    // side of a release-runtime device. Re-enable for targeted debug only.
    UINT flags = 0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, &_dx11Device, &got, &_dx11Context);
    if (FAILED(hr))
    {
        LOG_ERROR("Dx9wDx11: D3D11CreateDevice failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    LOG_DEBUG("Dx9wDx11: DX11 device created (FL=0x{:X})", static_cast<uint32_t>(got));
    return true;
}

bool IFeature_Dx9wDx11::CreateSharedColor()
{
    const DXGI_FORMAT dxgiFormat = MapD3D9FormatToDxgi(_gameFormat);
    if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Dx9wDx11: unsupported backbuffer format 0x{:X} — no DXGI mapping",
                  static_cast<uint32_t>(_gameFormat));
        return false;
    }

    auto createOne = [&](IDirect3DTexture9** outTex9, IDirect3DSurface9** outSurf9, HANDLE* outHandle,
                         ID3D11Texture2D** outTex11, const char* tag) -> bool {
        HANDLE shared = nullptr;
        HRESULT hr = _gameDeviceEx->CreateTexture(_width, _height, 1, D3DUSAGE_RENDERTARGET, _gameFormat,
                                                  D3DPOOL_DEFAULT, outTex9, &shared);
        if (FAILED(hr) || *outTex9 == nullptr || shared == nullptr)
        {
            LOG_ERROR("Dx9wDx11: CreateTexture(SHARED, {}) failed hr=0x{:08X}, handle={}", tag,
                      static_cast<uint32_t>(hr), shared);
            return false;
        }

        hr = (*outTex9)->GetSurfaceLevel(0, outSurf9);
        if (FAILED(hr) || *outSurf9 == nullptr)
        {
            LOG_ERROR("Dx9wDx11: GetSurfaceLevel({}) failed hr=0x{:08X}", tag, static_cast<uint32_t>(hr));
            return false;
        }

        *outHandle = shared;

        hr = _dx11Device->OpenSharedResource(shared, IID_PPV_ARGS(outTex11));
        if (FAILED(hr) || *outTex11 == nullptr)
        {
            LOG_ERROR("Dx9wDx11: OpenSharedResource({}) failed hr=0x{:08X}", tag, static_cast<uint32_t>(hr));
            return false;
        }

        return true;
    };

    if (!createOne(&_sharedInTex9, &_sharedInSurf9, &_sharedInHandle, &_sharedInTex11, "in"))
        return false;
    if (!createOne(&_sharedOutTex9, &_sharedOutSurf9, &_sharedOutHandle, &_sharedOutTex11, "out"))
        return false;

    LOG_DEBUG("Dx9wDx11: shared color in/out created (in handle={}, out handle={})", _sharedInHandle, _sharedOutHandle);
    return true;
}

bool IFeature_Dx9wDx11::WaitGpuDx9()
{
    if (_eventQuery == nullptr)
        return false;

    _eventQuery->Issue(D3DISSUE_END);

    // Spin with D3DGETDATA_FLUSH until the GPU drains. S_FALSE means "not yet",
    // S_OK means done. Bounded loop in case of a stuck driver — 100k iters is
    // plenty and avoids hanging the game forever.
    for (int i = 0; i < 100000; ++i)
    {
        const HRESULT hr = _eventQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK)
            return true;
        if (FAILED(hr))
        {
            LOG_ERROR("Dx9wDx11: GetData(EVENT) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
            return false;
        }
    }

    LOG_ERROR("Dx9wDx11: WaitGpuDx9 timed out");
    return false;
}

bool IFeature_Dx9wDx11::Render(IDirect3DSurface9* gameBackbuffer)
{
    if (!_init || gameBackbuffer == nullptr)
        return false;

    // 1. DX9: backbuffer -> shared in.
    HRESULT hr = _gameDevice->StretchRect(gameBackbuffer, nullptr, _sharedInSurf9, nullptr, D3DTEXF_POINT);
    if (FAILED(hr))
    {
        LOG_ERROR("Dx9wDx11: StretchRect(backbuf -> in) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // 2. Sync: drain DX9 pipeline so DX11 sees finished writes.
    if (!WaitGpuDx9())
        return false;

    // 3. DX11 work. In debug mode (Dx9TAA_BridgeDebug=true), clear the
    // shared output to bright red — impossible to miss visually. Otherwise,
    // round-trip via CopyResource so the bridge proves alive without
    // perturbing the image. Both end with _sharedOutTex11 ready for the
    // back-StretchRect.
    if (Config::Instance()->Dx9TAA_BridgeDebug.value_or_default() && _sharedOutRtv != nullptr)
    {
        const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        _dx11Context->ClearRenderTargetView(_sharedOutRtv, red);
    }
    else
    {
        _dx11Context->CopyResource(_sharedOutTex11, _sharedInTex11);
    }
    _dx11Context->Flush();

    // 4. DX9: shared out -> backbuffer. After this the screen content matches
    // what the game drew, having taken a detour through DX11 memory.
    hr = _gameDevice->StretchRect(_sharedOutSurf9, nullptr, gameBackbuffer, nullptr, D3DTEXF_POINT);
    if (FAILED(hr))
    {
        LOG_ERROR("Dx9wDx11: StretchRect(out -> backbuf) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    if (!_loggedRoundtripOk)
    {
        _loggedRoundtripOk = true;
        LOG_INFO("Dx9wDx11: first round-trip ok — DX9→DX11→DX9 path is live");
    }

    return true;
}

void IFeature_Dx9wDx11::ReleaseAll()
{
    auto safeRelease = [](auto*& p) {
        if (p != nullptr)
        {
            p->Release();
            p = nullptr;
        }
    };

    _sharpen.reset();

    safeRelease(_sharedOutRtv);
    safeRelease(_eventQuery);
    safeRelease(_sharedInSurf9);
    safeRelease(_sharedInTex9);
    safeRelease(_sharedOutSurf9);
    safeRelease(_sharedOutTex9);
    safeRelease(_sharedInTex11);
    safeRelease(_sharedOutTex11);
    safeRelease(_dx11Context);
    safeRelease(_dx11Device);

    _sharedInHandle = nullptr;
    _sharedOutHandle = nullptr;
    _init = false;
}
