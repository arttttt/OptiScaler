#include <pch.h>
#include "wrapped_d3d9_depthstencil.h"

WrappedDepthStencilSurface9::WrappedDepthStencilSurface9(IDirect3DSurface9* realIntz, const D3DSURFACE_DESC& reportedDesc)
    : _orig(realIntz), _reportedDesc(reportedDesc)
{
}

WrappedDepthStencilSurface9::~WrappedDepthStencilSurface9() = default;

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr)
        return E_POINTER;

    // Our own IID lets the device unwrap us back to the real INTZ surface
    // before forwarding to D3D9. Standard surface IIDs return ourselves so
    // games see the proxy uniformly.
    if (riid == __uuidof(WrappedDepthStencilSurface9) ||
        riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDirect3DResource9) ||
        riid == __uuidof(IDirect3DSurface9))
    {
        AddRef();
        *ppvObj = this;
        return S_OK;
    }

    return _orig->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE WrappedDepthStencilSurface9::AddRef()
{
    _orig->AddRef();
    return InterlockedIncrement(&_refcount);
}

ULONG STDMETHODCALLTYPE WrappedDepthStencilSurface9::Release()
{
    const ULONG ref = InterlockedDecrement(&_refcount);
    _orig->Release();
    if (ref == 0)
        delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetDevice(IDirect3DDevice9** ppDevice)
{
    return _orig->GetDevice(ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags)
{
    return _orig->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData)
{
    return _orig->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::FreePrivateData(REFGUID refguid)
{
    return _orig->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedDepthStencilSurface9::SetPriority(DWORD PriorityNew)
{
    return _orig->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetPriority()
{
    return _orig->GetPriority();
}

void STDMETHODCALLTYPE WrappedDepthStencilSurface9::PreLoad()
{
    _orig->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetType()
{
    return _orig->GetType();
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetContainer(REFIID riid, void** ppContainer)
{
    return _orig->GetContainer(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetDesc(D3DSURFACE_DESC* pDesc)
{
    if (pDesc == nullptr)
        return D3DERR_INVALIDCALL;

    // The whole reason this proxy exists: report what the game asked for,
    // not the INTZ format we actually allocated underneath.
    *pDesc = _reportedDesc;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
{
    return _orig->LockRect(pLockedRect, pRect, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::UnlockRect()
{
    return _orig->UnlockRect();
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::GetDC(HDC* phdc)
{
    return _orig->GetDC(phdc);
}

HRESULT STDMETHODCALLTYPE WrappedDepthStencilSurface9::ReleaseDC(HDC hdc)
{
    return _orig->ReleaseDC(hdc);
}
