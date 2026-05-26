#pragma once

#include <d3d9.h>

// Proxy for a depth-stencil surface that hides format substitution from the
// game. We back the real surface with an INTZ-format texture so we can sample
// it later in a copy pass, but games such as NFSU / Oblivion / Fallout NV
// query the surface's format and refuse to render correctly if they see
// anything other than what they originally asked for. GetDesc on this proxy
// returns the *original* description (D24S8 or whatever the game requested),
// while every other method forwards to the real INTZ surface.
//
// Ported from ReShade's Direct3DDepthStencilSurface9 (BSD 3-Clause, attribution
// to Patrick Mours / crosire) — see source/d3d9/d3d9_resource.{hpp,cpp} upstream.
class __declspec(uuid("0F433AEB-B389-4589-81A7-9DB59F34CB55"))
    WrappedDepthStencilSurface9 final : public IDirect3DSurface9
{
public:
    WrappedDepthStencilSurface9(IDirect3DSurface9* realIntz, const D3DSURFACE_DESC& reportedDesc);
    virtual ~WrappedDepthStencilSurface9();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDirect3DResource9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
    DWORD STDMETHODCALLTYPE GetPriority() override;
    void STDMETHODCALLTYPE PreLoad() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

    // IDirect3DSurface9
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void** ppContainer) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC* phdc) override;
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) override;

    IDirect3DSurface9* RealSurface() const { return _orig; }
    const D3DSURFACE_DESC& ReportedDesc() const { return _reportedDesc; }

private:
    IDirect3DSurface9* _orig;
    D3DSURFACE_DESC _reportedDesc;
    LONG _refcount = 1;
};
