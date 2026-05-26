#include <pch.h>
#include "wrapped_d3d9_device.h"

#include "misc/HaltonSequence.h"

WrappedIDirect3DDevice9Ex::WrappedIDirect3DDevice9Ex(IDirect3DDevice9* real, IDirect3DDevice9Ex* realEx, HWND hwnd, D3DPRESENT_PARAMETERS* pPresentParams)
    : _real(real), _realEx(realEx), _hwnd(hwnd)
{
    if (pPresentParams)
        _presentParams = *pPresentParams;

    ZeroMemory(&_currentProjection, sizeof(D3DMATRIX));
    ZeroMemory(&_currentView, sizeof(D3DMATRIX));
    ZeroMemory(&_currentWorld, sizeof(D3DMATRIX));
}

WrappedIDirect3DDevice9Ex::~WrappedIDirect3DDevice9Ex()
{
    InvalidateTrackedResources();
}

void WrappedIDirect3DDevice9Ex::InvalidateTrackedResources()
{
    if (_trackedDepthSurface)
    {
        _trackedDepthSurface->Release();
        _trackedDepthSurface = nullptr;
    }
    _trackedDepthArea = 0;
    _trackedDepthDesc = {};
    _loggedDepthCapture = false;

    if (_depthStagingSurface)
    {
        _depthStagingSurface->Release();
        _depthStagingSurface = nullptr;
    }
    _loggedDepthReadback = false;
}

void WrappedIDirect3DDevice9Ex::AttemptDepthReadback()
{
    if (!_trackedDepthSurface)
        return;

    // MSAA depth needs resolve via StretchRect first — out of scope for Phase 3 MVP.
    if (_trackedDepthDesc.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        if (!_loggedDepthReadback)
        {
            _loggedDepthReadback = true;
            LOG_WARN("Depth readback skipped: MSAA depth surface ({}x{} samples={})",
                     _trackedDepthDesc.Width, _trackedDepthDesc.Height,
                     static_cast<int>(_trackedDepthDesc.MultiSampleType));
        }
        return;
    }

    // Staging surface: same format/dims, SYSTEMMEM. Created lazily, freed on Reset.
    if (!_depthStagingSurface)
    {
        HRESULT hr = _real->CreateOffscreenPlainSurface(
            _trackedDepthDesc.Width,
            _trackedDepthDesc.Height,
            _trackedDepthDesc.Format,
            D3DPOOL_SYSTEMMEM,
            &_depthStagingSurface,
            nullptr);

        if (FAILED(hr))
        {
            if (!_loggedDepthReadback)
            {
                _loggedDepthReadback = true;
                LOG_ERROR("Depth staging CreateOffscreenPlainSurface failed: hr=0x{:08X} (format=0x{:X})",
                          static_cast<uint32_t>(hr),
                          static_cast<uint32_t>(_trackedDepthDesc.Format));
            }
            return;
        }
    }

    const HRESULT hr = _real->GetRenderTargetData(_trackedDepthSurface, _depthStagingSurface);

    if (!_loggedDepthReadback)
    {
        _loggedDepthReadback = true;
        if (SUCCEEDED(hr))
            LOG_INFO("Depth GetRenderTargetData OK ({}x{}, format=0x{:X})",
                     _trackedDepthDesc.Width, _trackedDepthDesc.Height,
                     static_cast<uint32_t>(_trackedDepthDesc.Format));
        else
            LOG_WARN("Depth GetRenderTargetData failed: hr=0x{:08X} — likely needs INTZ/Nukem path",
                     static_cast<uint32_t>(hr));
    }
}

// =============================================================================
// IUnknown
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IDirect3DDevice9))
    {
        AddRef();
        *ppvObject = static_cast<IDirect3DDevice9*>(this);
        return S_OK;
    }
    else if (riid == __uuidof(IDirect3DDevice9Ex) && _realEx != nullptr)
    {
        AddRef();
        *ppvObject = static_cast<IDirect3DDevice9Ex*>(this);
        return S_OK;
    }
    else if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *ppvObject = static_cast<IUnknown*>(this);
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::AddRef()
{
    return InterlockedIncrement(&_refcount);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Release()
{
    ULONG ref = InterlockedDecrement(&_refcount);
    if (ref == 0)
    {
        _real->Release();
        delete this;
    }
    return ref;
}

// =============================================================================
// IDirect3DDevice9 methods
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::TestCooperativeLevel()
{
    return _real->TestCooperativeLevel();
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetAvailableTextureMem()
{
    return _real->GetAvailableTextureMem();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EvictManagedResources()
{
    return _real->EvictManagedResources();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDirect3D(IDirect3D9** ppD3D9)
{
    return _real->GetDirect3D(ppD3D9);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDeviceCaps(D3DCAPS9* pCaps)
{
    return _real->GetDeviceCaps(pCaps);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode)
{
    return _real->GetDisplayMode(iSwapChain, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters)
{
    return _real->GetCreationParameters(pParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
    return _real->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCursorPosition(int X, int Y, DWORD Flags)
{
    _real->SetCursorPosition(X, Y, Flags);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ShowCursor(BOOL bShow)
{
    return _real->ShowCursor(bShow);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain)
{
    return _real->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain)
{
    return _real->GetSwapChain(iSwapChain, pSwapChain);
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetNumberOfSwapChains()
{
    return _real->GetNumberOfSwapChains();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    InvalidateTrackedResources();

    if (pPresentationParameters)
        _presentParams = *pPresentationParameters;

    return _real->Reset(pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    if (Config::Instance()->Dx9TAA.value_or_default())
        AttemptDepthReadback();

    return _real->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer)
{
    return _real->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus)
{
    return _real->GetRasterStatus(iSwapChain, pRasterStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetDialogBoxMode(BOOL bEnableDialogs)
{
    return _real->SetDialogBoxMode(bEnableDialogs);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp)
{
    _real->SetGammaRamp(iSwapChain, Flags, pRamp);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp)
{
    _real->GetGammaRamp(iSwapChain, pRamp);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
    return _real->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
{
    return _real->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
    return _real->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
    return _real->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
    return _real->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    return _real->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    return _real->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint)
{
    return _real->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture)
{
    return _real->UpdateTexture(pSourceTexture, pDestinationTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface)
{
    return _real->GetRenderTargetData(pRenderTarget, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
    return _real->GetFrontBufferData(iSwapChain, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
    return _real->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color)
{
    return _real->ColorFill(pSurface, pRect, color);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    return _real->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
    return _real->SetRenderTarget(RenderTargetIndex, pRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget)
{
    return _real->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil)
{
    if (pNewZStencil)
    {
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(pNewZStencil->GetDesc(&desc)))
        {
            const UINT area = desc.Width * desc.Height;

            // Filter: only consider depth surfaces matching backbuffer dimensions.
            // Skips shadow maps, post-process depth copies, etc.
            const bool matchesBackbuffer =
                _presentParams.BackBufferWidth > 0 &&
                desc.Width == _presentParams.BackBufferWidth &&
                desc.Height == _presentParams.BackBufferHeight;

            if (matchesBackbuffer && area > _trackedDepthArea)
            {
                if (_trackedDepthSurface)
                    _trackedDepthSurface->Release();

                _trackedDepthSurface = pNewZStencil;
                _trackedDepthSurface->AddRef();
                _trackedDepthArea = area;
                _trackedDepthDesc = desc;

                if (!_loggedDepthCapture)
                {
                    _loggedDepthCapture = true;
                    LOG_INFO("Tracked scene depth surface: {}x{}, format=0x{:X}, MS={}",
                             desc.Width, desc.Height,
                             static_cast<uint32_t>(desc.Format),
                             static_cast<int>(desc.MultiSampleType));
                }
            }
        }
    }

    return _real->SetDepthStencilSurface(pNewZStencil);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface)
{
    return _real->GetDepthStencilSurface(ppZStencilSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::BeginScene()
{
    // Tracking stub: increment frame index
    _frameIndex++;

    return _real->BeginScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EndScene()
{
    if (Config::Instance()->Dx9TAA.value_or_default())
    {
        // Flashing 16x16 square in the top-left corner: cycles R/G/B per frame
        // so the user can confirm the Dx9TAA path is live without reading the log.
        const D3DRECT indicator = { 8, 8, 24, 24 };
        const D3DCOLOR colors[3] = { D3DCOLOR_XRGB(255, 0, 0), D3DCOLOR_XRGB(0, 255, 0), D3DCOLOR_XRGB(0, 0, 255) };
        HRESULT hr = _real->Clear(1, &indicator, D3DCLEAR_TARGET, colors[_frameIndex % 3], 0.0f, 0);

        if (!_loggedIndicatorDraw)
        {
            _loggedIndicatorDraw = true;
            LOG_INFO("Dx9TAA: first indicator Clear, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    return _real->EndScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
    return _real->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    if (pMatrix)
    {
        switch (State)
        {
        case D3DTS_VIEW:
            _currentView = *pMatrix;
            break;
        case D3DTS_PROJECTION:
            _currentProjection = *pMatrix;

            if (Config::Instance()->Dx9TAA.value_or_default())
            {
                const UINT width = _presentParams.BackBufferWidth;
                const UINT height = _presentParams.BackBufferHeight;

                if (width > 0 && height > 0)
                {
                    // DLAA = scale 1.0 → 8 phases (AMD FSR2 formula ceil(8 * n²))
                    constexpr int32_t phaseCount = 8;
                    HaltonSequence::GetJitterOffset(_frameIndex, phaseCount, &_jitterX, &_jitterY);

                    // Y sign is AMD's DX12 reference; DX9 Y-convention may flip — verify visually.
                    const float clipX = 2.0f * _jitterX / static_cast<float>(width);
                    const float clipY = -2.0f * _jitterY / static_cast<float>(height);

                    D3DMATRIX jittered = *pMatrix;
                    jittered._31 += clipX;
                    jittered._32 += clipY;

                    if (!_loggedProjectionJitter)
                    {
                        _loggedProjectionJitter = true;
                        LOG_INFO("Dx9TAA: first projection jitter applied (px=[{:.3f},{:.3f}], clip=[{:.6f},{:.6f}], backbuf={}x{})",
                                 _jitterX, _jitterY, clipX, clipY, width, height);
                    }

                    return _real->SetTransform(State, &jittered);
                }
            }
            break;
        case D3DTS_WORLD:
            _currentWorld = *pMatrix;
            break;
        default:
            break;
        }
    }

    return _real->SetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)
{
    return _real->GetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    return _real->MultiplyTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetViewport(CONST D3DVIEWPORT9* pViewport)
{
    return _real->SetViewport(pViewport);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetViewport(D3DVIEWPORT9* pViewport)
{
    return _real->GetViewport(pViewport);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetMaterial(CONST D3DMATERIAL9* pMaterial)
{
    return _real->SetMaterial(pMaterial);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetMaterial(D3DMATERIAL9* pMaterial)
{
    return _real->GetMaterial(pMaterial);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetLight(DWORD Index, CONST D3DLIGHT9* pLight)
{
    return _real->SetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetLight(DWORD Index, D3DLIGHT9* pLight)
{
    return _real->GetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::LightEnable(DWORD Index, BOOL Enable)
{
    return _real->LightEnable(Index, Enable);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetLightEnable(DWORD Index, BOOL* pEnable)
{
    return _real->GetLightEnable(Index, pEnable);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetClipPlane(DWORD Index, CONST float* pPlane)
{
    return _real->SetClipPlane(Index, pPlane);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetClipPlane(DWORD Index, float* pPlane)
{
    return _real->GetClipPlane(Index, pPlane);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
    return _real->SetRenderState(State, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue)
{
    return _real->GetRenderState(State, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB)
{
    return _real->CreateStateBlock(Type, ppSB);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::BeginStateBlock()
{
    return _real->BeginStateBlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EndStateBlock(IDirect3DStateBlock9** ppSB)
{
    return _real->EndStateBlock(ppSB);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus)
{
    return _real->SetClipStatus(pClipStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetClipStatus(D3DCLIPSTATUS9* pClipStatus)
{
    return _real->GetClipStatus(pClipStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture)
{
    return _real->GetTexture(Stage, ppTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture)
{
    return _real->SetTexture(Stage, pTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue)
{
    return _real->GetTextureStageState(Stage, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    return _real->SetTextureStageState(Stage, Type, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue)
{
    return _real->GetSamplerState(Sampler, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
    return _real->SetSamplerState(Sampler, Type, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ValidateDevice(DWORD* pNumPasses)
{
    return _real->ValidateDevice(pNumPasses);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries)
{
    return _real->SetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries)
{
    return _real->GetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCurrentTexturePalette(UINT PaletteNumber)
{
    return _real->SetCurrentTexturePalette(PaletteNumber);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetCurrentTexturePalette(UINT* PaletteNumber)
{
    return _real->GetCurrentTexturePalette(PaletteNumber);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetScissorRect(CONST RECT* pRect)
{
    return _real->SetScissorRect(pRect);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetScissorRect(RECT* pRect)
{
    return _real->GetScissorRect(pRect);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetSoftwareVertexProcessing(BOOL bSoftware)
{
    return _real->SetSoftwareVertexProcessing(bSoftware);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSoftwareVertexProcessing()
{
    return _real->GetSoftwareVertexProcessing();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetNPatchMode(float nSegments)
{
    return _real->SetNPatchMode(nSegments);
}

float STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetNPatchMode()
{
    return _real->GetNPatchMode();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    return _real->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
    return _real->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    return _real->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    return _real->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags)
{
    return _real->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl)
{
    return _real->CreateVertexDeclaration(pVertexElements, ppDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl)
{
    return _real->SetVertexDeclaration(pDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl)
{
    return _real->GetVertexDeclaration(ppDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetFVF(DWORD FVF)
{
    return _real->SetFVF(FVF);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetFVF(DWORD* pFVF)
{
    return _real->GetFVF(pFVF);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader)
{
    return _real->CreateVertexShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShader(IDirect3DVertexShader9* pShader)
{
    return _real->SetVertexShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShader(IDirect3DVertexShader9** ppShader)
{
    return _real->GetVertexShader(ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
    return _real->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
    return _real->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
    return _real->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
    return _real->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
    return _real->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
    return _real->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
{
    return _real->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride)
{
    return _real->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetStreamSourceFreq(UINT StreamNumber, UINT Setting)
{
    return _real->SetStreamSourceFreq(StreamNumber, Setting);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting)
{
    return _real->GetStreamSourceFreq(StreamNumber, pSetting);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetIndices(IDirect3DIndexBuffer9* pIndexData)
{
    return _real->SetIndices(pIndexData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetIndices(IDirect3DIndexBuffer9** ppIndexData)
{
    return _real->GetIndices(ppIndexData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
    return _real->CreatePixelShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShader(IDirect3DPixelShader9* pShader)
{
    return _real->SetPixelShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShader(IDirect3DPixelShader9** ppShader)
{
    return _real->GetPixelShader(ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
    return _real->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
    return _real->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
    return _real->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
    return _real->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
    return _real->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
    return _real->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
    return _real->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
    return _real->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DeletePatch(UINT Handle)
{
    return _real->DeletePatch(Handle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery)
{
    return _real->CreateQuery(Type, ppQuery);
}

// =============================================================================
// IDirect3DDevice9Ex methods
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetConvolutionMonoKernel(UINT width, UINT height, float* rows, float* columns)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetConvolutionMonoKernel(width, height, rows, columns);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ComposeRects(IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->ComposeRects(pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs, Operation, Xoffset, Yoffset);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::PresentEx(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    if (Config::Instance()->Dx9TAA.value_or_default())
        AttemptDepthReadback();

    return _realEx->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetGPUThreadPriority(INT* pPriority)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetGPUThreadPriority(pPriority);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetGPUThreadPriority(INT Priority)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetGPUThreadPriority(Priority);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::WaitForVBlank(UINT iSwapChain)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->WaitForVBlank(iSwapChain);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CheckResourceResidency(IDirect3DResource9** pResourceArray, UINT32 NumResources)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CheckResourceResidency(pResourceArray, NumResources);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetMaximumFrameLatency(UINT MaxLatency)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetMaximumFrameLatency(pMaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CheckDeviceState(HWND hDestinationWindow)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CheckDeviceState(hDestinationWindow);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CreateRenderTargetEx(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CreateDepthStencilSurfaceEx(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ResetEx(D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    InvalidateTrackedResources();

    if (pPresentationParameters)
        _presentParams = *pPresentationParameters;

    return _realEx->ResetEx(pPresentationParameters, pFullscreenDisplayMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetDisplayModeEx(iSwapChain, pMode, pRotation);
}
