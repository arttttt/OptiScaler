#include "pch.h"
#include "IFeature_Dx9wDx11.h"

#include <Config.h>
#include <shaders/sharpen/Sharpen_Dx11.h>

#include <fsr2/ffx_fsr2.h>
#include <fsr2/dx11/ffx_fsr2_dx11.h>

#include <vector>

// Pimpl for the FSR2 context (see the header for why it's hidden here). The
// scratch buffer backs the FSR2 interface for the context's whole lifetime,
// so it must outlive the context — ReleaseAll destroys the context before
// this struct (and its scratch) is freed.
struct Dx9wDx11Fsr2State
{
    FfxFsr2Context context {};
    FfxFsr2ContextDescription desc {};
    std::vector<uint8_t> scratch;
    bool created = false;
};

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

    // Non-fatal: if the depth share can't be set up, the bridge still does
    // its round-trip; FSR2 just won't have a depth input until it's fixed.
    if (!CreateSharedDepth())
        LOG_WARN("Dx9wDx11: shared depth unavailable — FSR2 will lack depth");

    const HRESULT qhr = _gameDevice->CreateQuery(D3DQUERYTYPE_EVENT, &_eventQuery);
    if (FAILED(qhr) || _eventQuery == nullptr)
    {
        LOG_ERROR("Dx9wDx11: CreateQuery(EVENT) failed hr=0x{:08X}", static_cast<uint32_t>(qhr));
        ReleaseAll();
        return false;
    }

    // 5b RTV (second attempt): debug layer is now off so any RTV-create
    // failure comes back as hr instead of __debugbreak(). Passing nullptr
    // for the view desc lets D3D11 pick the default that matches the
    // texture's format exactly — avoids any DXGI mapping mismatch.
    LOG_INFO("Dx9wDx11 Init: about to CreateRenderTargetView");
    HRESULT rtvHr = _dx11Device->CreateRenderTargetView(_sharedOutTex11, nullptr, &_sharedOutRtv);
    if (FAILED(rtvHr))
    {
        LOG_WARN("Dx9wDx11: shared-out RTV creation failed hr=0x{:08X} — debug clear disabled",
                 static_cast<uint32_t>(rtvHr));
        _sharedOutRtv = nullptr;
    }
    LOG_INFO("Dx9wDx11 Init: CreateRenderTargetView done (rtv={})", _sharedOutRtv != nullptr ? "ok" : "null");

    // Phase 5b: stand up the FSR2 context on the bridge's DX11 device. Raw
    // ffx_fsr2 API (see header). Non-fatal — if FSR2 can't init the bridge
    // still does its round-trip; the per-frame dispatch lands next.
    InitFsr2();

    _init = true;
    LOG_INFO("Dx9wDx11 bridge initialised ({}x{}, fmt=0x{:X}, rtv={})", _width, _height,
             static_cast<uint32_t>(_gameFormat), _sharedOutRtv != nullptr ? "ok" : "none");
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

// 5b: shared R32F scene depth, DX9 -> DX11. The Phase 3 depth-copy already
// produces R32F, so the per-frame StretchRect into this is a same-format
// copy. RENDERTARGET usage makes it both a valid StretchRect destination and
// openable as a DX11 SRV for FSR2.
bool IFeature_Dx9wDx11::CreateSharedDepth()
{
    HANDLE shared = nullptr;
    HRESULT hr = _gameDeviceEx->CreateTexture(_width, _height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                              D3DPOOL_DEFAULT, &_sharedDepthTex9, &shared);
    if (FAILED(hr) || _sharedDepthTex9 == nullptr || shared == nullptr)
    {
        LOG_ERROR("Dx9wDx11: CreateTexture(SHARED depth R32F) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    hr = _sharedDepthTex9->GetSurfaceLevel(0, &_sharedDepthSurf9);
    if (FAILED(hr) || _sharedDepthSurf9 == nullptr)
    {
        LOG_ERROR("Dx9wDx11: GetSurfaceLevel(depth) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    _sharedDepthHandle = shared;

    hr = _dx11Device->OpenSharedResource(shared, IID_PPV_ARGS(&_sharedDepthTex11));
    if (FAILED(hr) || _sharedDepthTex11 == nullptr)
    {
        LOG_ERROR("Dx9wDx11: OpenSharedResource(depth) failed hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    LOG_DEBUG("Dx9wDx11: shared R32F depth created (handle={})", _sharedDepthHandle);
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

bool IFeature_Dx9wDx11::Render(IDirect3DSurface9* gameBackbuffer, IDirect3DSurface9* gameDepthR32f)
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

    // 1b. DX9: scene depth -> shared depth (R32F same-format copy). Skipped
    // when no depth was identified this frame. Done before the sync so the
    // single WaitGpuDx9 covers both color and depth.
    if (gameDepthR32f != nullptr && _sharedDepthSurf9 != nullptr)
    {
        HRESULT dhr = _gameDevice->StretchRect(gameDepthR32f, nullptr, _sharedDepthSurf9, nullptr, D3DTEXF_POINT);
        if (!_loggedDepthShare)
        {
            _loggedDepthShare = true;
            if (SUCCEEDED(dhr))
                LOG_INFO("Dx9wDx11: scene depth bridged to DX11 (R32F)");
            else
                LOG_WARN("Dx9wDx11: StretchRect(depth -> shared) failed hr=0x{:08X} — FSR2 depth stale",
                         static_cast<uint32_t>(dhr));
        }
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

// Phase 5b: create the FSR2 context on the bridge's DX11 device via the raw
// ffx_fsr2 API. DLAA = no upscaling, so render size == display size == the
// backbuffer size. Non-fatal: returns false (and leaves _fsr2 null) on any
// failure; the caller logs and keeps the bridge round-trip alive.
bool IFeature_Dx9wDx11::InitFsr2()
{
    if (_dx11Device == nullptr)
        return false;

    _fsr2 = std::make_unique<Dx9wDx11Fsr2State>();

    const size_t scratchSize = ffxFsr2GetScratchMemorySizeDX11();
    _fsr2->scratch.resize(scratchSize);

    FfxErrorCode err =
        ffxFsr2GetInterfaceDX11(&_fsr2->desc.callbacks, _dx11Device, _fsr2->scratch.data(), scratchSize);
    if (err != FFX_OK)
    {
        LOG_WARN("Dx9wDx11: ffxFsr2GetInterfaceDX11 failed ({}) — FSR2 off, bridge keeps round-trip",
                 static_cast<int>(err));
        _fsr2.reset();
        return false;
    }

    _fsr2->desc.device = ffxGetDeviceDX11(_dx11Device);
    // Auto-exposure is the safe default; depth-inverted / HDR get calibrated
    // with the per-frame dispatch once real inputs flow in.
    _fsr2->desc.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    _fsr2->desc.maxRenderSize.width = _width;
    _fsr2->desc.maxRenderSize.height = _height;
    _fsr2->desc.displaySize.width = _width;   // DLAA: 1:1, no upscaling
    _fsr2->desc.displaySize.height = _height;
    _fsr2->desc.fpMessage = nullptr;

    err = ffxFsr2ContextCreate(&_fsr2->context, &_fsr2->desc);
    if (err != FFX_OK)
    {
        LOG_WARN("Dx9wDx11: ffxFsr2ContextCreate failed ({}) — FSR2 off, bridge keeps round-trip",
                 static_cast<int>(err));
        _fsr2.reset();
        return false;
    }

    _fsr2->created = true;
    LOG_INFO("Dx9wDx11: FSR2 context created (DLAA {}x{}, scratch {} KB) — x86 FSR2 is live", _width, _height,
             static_cast<uint32_t>(scratchSize / 1024));
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

    // FSR2 first: its callbacks reference the DX11 device, which is released
    // below, so the context must be destroyed while the device is still alive.
    if (_fsr2 != nullptr)
    {
        if (_fsr2->created)
            ffxFsr2ContextDestroy(&_fsr2->context);
        _fsr2.reset();
    }

    _sharpen.reset();

    safeRelease(_sharedOutRtv);
    safeRelease(_eventQuery);
    safeRelease(_sharedInSurf9);
    safeRelease(_sharedInTex9);
    safeRelease(_sharedOutSurf9);
    safeRelease(_sharedOutTex9);
    safeRelease(_sharedInTex11);
    safeRelease(_sharedOutTex11);
    safeRelease(_sharedDepthSurf9);
    safeRelease(_sharedDepthTex9);
    safeRelease(_sharedDepthTex11);
    safeRelease(_dx11Context);
    safeRelease(_dx11Device);

    _sharedInHandle = nullptr;
    _sharedOutHandle = nullptr;
    _sharedDepthHandle = nullptr;
    _init = false;
}
