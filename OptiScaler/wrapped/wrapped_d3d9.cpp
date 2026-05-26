#include "pch.h"
#include "wrapped_d3d9.h"
#include "wrapped_d3d9_device.h"

#include <State.h>

WrappedIDirect3D9Ex::WrappedIDirect3D9Ex(IDirect3D9* real) : _real(real), _realEx(nullptr), _refcount(1)
{
    LOG_FUNC();
    real->QueryInterface(IID_IDirect3D9Ex, (void**) &_realEx);
    if (_realEx)
        LOG_INFO("IDirect3D9Ex interface available");
    else
        LOG_INFO("IDirect3D9 interface only (no Ex)");
}

WrappedIDirect3D9Ex::~WrappedIDirect3D9Ex()
{
    LOG_FUNC();
    if (_realEx)
        _realEx->Release();
    // Don't release _real separately if _realEx was obtained from it via QI
    // (QI AddRef'd _realEx, and _real is the same underlying object)
    if (!_realEx && _real)
        _real->Release();
}

// IUnknown

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IDirect3D9)
    {
        AddRef();
        *ppvObject = static_cast<IDirect3D9*>(this);
        return S_OK;
    }

    if (riid == IID_IDirect3D9Ex && _realEx != nullptr)
    {
        AddRef();
        *ppvObject = static_cast<IDirect3D9Ex*>(this);
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3D9Ex::AddRef()
{
    return InterlockedIncrement(&_refcount);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3D9Ex::Release()
{
    ULONG ref = InterlockedDecrement(&_refcount);
    if (ref == 0)
    {
        LOG_INFO("WrappedIDirect3D9Ex released");
        delete this;
    }
    return ref;
}

// IDirect3D9

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::RegisterSoftwareDevice(void* pInitializeFunction)
{
    return _real->RegisterSoftwareDevice(pInitializeFunction);
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterCount()
{
    return _real->GetAdapterCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterIdentifier(UINT Adapter, DWORD Flags,
                                                                     D3DADAPTER_IDENTIFIER9* pIdentifier)
{
    return _real->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format)
{
    return _real->GetAdapterModeCount(Adapter, Format);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode,
                                                                 D3DDISPLAYMODE* pMode)
{
    return _real->EnumAdapterModes(Adapter, Format, Mode, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode)
{
    return _real->GetAdapterDisplayMode(Adapter, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType,
                                                                D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat,
                                                                BOOL bWindowed)
{
    return _real->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType,
                                                                  D3DFORMAT AdapterFormat, DWORD Usage,
                                                                  D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
    return _real->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType,
                                                                           D3DFORMAT SurfaceFormat, BOOL Windowed,
                                                                           D3DMULTISAMPLE_TYPE MultiSampleType,
                                                                           DWORD* pQualityLevels)
{
    return _real->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType,
                                              pQualityLevels);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType,
                                                                       D3DFORMAT AdapterFormat,
                                                                       D3DFORMAT RenderTargetFormat,
                                                                       D3DFORMAT DepthStencilFormat)
{
    return _real->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType,
                                                                            D3DFORMAT SourceFormat,
                                                                            D3DFORMAT TargetFormat)
{
    return _real->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps)
{
    return _real->GetDeviceCaps(Adapter, DeviceType, pCaps);
}

HMONITOR STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterMonitor(UINT Adapter)
{
    return _real->GetAdapterMonitor(Adapter);
}

// Phase 3 INTZ Mark 2: helper used by both CreateDevice and CreateDeviceEx
// to decide if we should suppress D3D9's auto-depth in favour of our own INTZ.
static bool ShouldSubstituteAutoDepth(IDirect3D9* d3d9, UINT adapter, D3DDEVTYPE deviceType,
                                      const D3DPRESENT_PARAMETERS* pp)
{
    if (!Config::Instance()->Dx9TAA.value_or_default()) return false;
    if (!Config::Instance()->Dx9TAA_AutoDepthSubstitution.value_or_default()) return false;
    if (pp == nullptr) return false;
    if (!pp->EnableAutoDepthStencil) return false;
    if (pp->MultiSampleType != D3DMULTISAMPLE_NONE) return false;
    if (pp->AutoDepthStencilFormat != D3DFMT_D24S8 &&
        pp->AutoDepthStencilFormat != D3DFMT_D24X8)
        return false;

    D3DDISPLAYMODE mode = {};
    if (FAILED(d3d9->GetAdapterDisplayMode(adapter, &mode))) return false;

    const D3DFORMAT intzFormat = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
    const HRESULT hr = d3d9->CheckDeviceFormat(adapter, deviceType, mode.Format,
                                                D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, intzFormat);
    return SUCCEEDED(hr);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                                             DWORD BehaviorFlags,
                                                             D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                             IDirect3DDevice9** ppReturnedDeviceInterface)
{
    LOG_FUNC();
    LOG_INFO("CreateDevice called, Adapter: {}, DeviceType: {}", Adapter, (int) DeviceType);

    // Force multithreaded for DX12 interop
    BehaviorFlags |= D3DCREATE_MULTITHREADED;

    // Phase 3 INTZ Mark 2: suppress auto-depth in eligible configurations so
    // we can re-create the depth as an INTZ-backed texture (sampleable).
    D3DPRESENT_PARAMETERS originalParams = {};
    bool autoDepthSubstituted = false;
    if (ShouldSubstituteAutoDepth(_real, Adapter, DeviceType, pPresentationParameters))
    {
        originalParams = *pPresentationParameters;
        pPresentationParameters->EnableAutoDepthStencil = FALSE;
        autoDepthSubstituted = true;
        LOG_INFO("Auto-depth suppressed for INTZ substitution (orig format=0x{:X})",
                 static_cast<uint32_t>(originalParams.AutoDepthStencilFormat));
    }

    HRESULT hr = _real->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
                                      ppReturnedDeviceInterface);

    if (FAILED(hr) || ppReturnedDeviceInterface == nullptr || *ppReturnedDeviceInterface == nullptr)
    {
        if (autoDepthSubstituted)
            *pPresentationParameters = originalParams; // restore for caller's retry
        LOG_ERROR("CreateDevice failed: {:#x}", (unsigned long) hr);
        return hr;
    }

    LOG_INFO("Wrapping IDirect3DDevice9");
    IDirect3DDevice9Ex* deviceEx = nullptr;
    (*ppReturnedDeviceInterface)->QueryInterface(IID_IDirect3DDevice9Ex, (void**) &deviceEx);

    auto wrapped =
        new WrappedIDirect3DDevice9Ex(*ppReturnedDeviceInterface, deviceEx, hFocusWindow, pPresentationParameters);

    if (autoDepthSubstituted)
    {
        // InitAutoDepthIntz sets _autoDepthSubstituted internally so Reset
        // can re-run us on the next device state.
        wrapped->InitAutoDepthIntz(pPresentationParameters->BackBufferWidth,
                                   pPresentationParameters->BackBufferHeight);
    }

    State::Instance().currentD3D9Device = wrapped;
    State::Instance().swapchainApi = API::DX9;
    State::Instance().d3d9Devices.push_back(wrapped);

    *ppReturnedDeviceInterface = wrapped;
    return hr;
}

// IDirect3D9Ex

UINT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterModeCountEx(UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter)
{
    if (_realEx)
        return _realEx->GetAdapterModeCountEx(Adapter, pFilter);
    return 0;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::EnumAdapterModesEx(UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter,
                                                                   UINT Mode, D3DDISPLAYMODEEX* pMode)
{
    if (_realEx)
        return _realEx->EnumAdapterModesEx(Adapter, pFilter, Mode, pMode);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX* pMode,
                                                                        D3DDISPLAYROTATION* pRotation)
{
    if (_realEx)
        return _realEx->GetAdapterDisplayModeEx(Adapter, pMode, pRotation);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                                               DWORD BehaviorFlags,
                                                               D3DPRESENT_PARAMETERS* pPresentationParameters,
                                                               D3DDISPLAYMODEEX* pFullscreenDisplayMode,
                                                               IDirect3DDevice9Ex** ppReturnedDeviceInterface)
{
    LOG_FUNC();
    LOG_INFO("CreateDeviceEx called, Adapter: {}, DeviceType: {}", Adapter, (int) DeviceType);

    if (_realEx == nullptr)
    {
        LOG_ERROR("CreateDeviceEx called but no IDirect3D9Ex available!");
        return E_NOTIMPL;
    }

    // Force multithreaded for DX12 interop
    BehaviorFlags |= D3DCREATE_MULTITHREADED;

    // Phase 3 INTZ Mark 2: same auto-depth suppression as the non-Ex path.
    D3DPRESENT_PARAMETERS originalParams = {};
    bool autoDepthSubstituted = false;
    if (ShouldSubstituteAutoDepth(_real, Adapter, DeviceType, pPresentationParameters))
    {
        originalParams = *pPresentationParameters;
        pPresentationParameters->EnableAutoDepthStencil = FALSE;
        autoDepthSubstituted = true;
        LOG_INFO("Auto-depth suppressed for INTZ substitution via Ex (orig format=0x{:X})",
                 static_cast<uint32_t>(originalParams.AutoDepthStencilFormat));
    }

    HRESULT hr = _realEx->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
                                          pFullscreenDisplayMode, ppReturnedDeviceInterface);

    if (FAILED(hr) || ppReturnedDeviceInterface == nullptr || *ppReturnedDeviceInterface == nullptr)
    {
        if (autoDepthSubstituted)
            *pPresentationParameters = originalParams;
        LOG_ERROR("CreateDeviceEx failed: {:#x}", (unsigned long) hr);
        return hr;
    }

    LOG_INFO("Wrapping IDirect3DDevice9Ex");
    auto wrapped = new WrappedIDirect3DDevice9Ex(static_cast<IDirect3DDevice9*>(*ppReturnedDeviceInterface),
                                                  *ppReturnedDeviceInterface, hFocusWindow, pPresentationParameters);

    if (autoDepthSubstituted)
    {
        wrapped->InitAutoDepthIntz(pPresentationParameters->BackBufferWidth,
                                   pPresentationParameters->BackBufferHeight);
    }

    State::Instance().currentD3D9Device = wrapped;
    State::Instance().currentD3D9DeviceEx = wrapped;
    State::Instance().swapchainApi = API::DX9;
    State::Instance().d3d9Devices.push_back(wrapped);

    *ppReturnedDeviceInterface = wrapped;
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9Ex::GetAdapterLUID(UINT Adapter, LUID* pLUID)
{
    if (_realEx)
        return _realEx->GetAdapterLUID(Adapter, pLUID);
    return E_NOTIMPL;
}
