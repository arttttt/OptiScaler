#pragma once

#include "SysUtils.h"

#include <proxies/D3D9_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <wrapped/wrapped_d3d9.h>

struct d3d9_dll
{
    HMODULE dll = nullptr;
    FARPROC D3DPERF_BeginEvent = nullptr;
    FARPROC D3DPERF_EndEvent = nullptr;
    FARPROC D3DPERF_SetMarker = nullptr;
    FARPROC D3DPERF_SetRegion = nullptr;
    FARPROC D3DPERF_QueryRepeatFrame = nullptr;
    FARPROC D3DPERF_SetOptions = nullptr;
    FARPROC D3DPERF_GetStatus = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        dll = module;

        D3DPERF_BeginEvent = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_BeginEvent");
        D3DPERF_EndEvent = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_EndEvent");
        D3DPERF_SetMarker = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_SetMarker");
        D3DPERF_SetRegion = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_SetRegion");
        D3DPERF_QueryRepeatFrame = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_QueryRepeatFrame");
        D3DPERF_SetOptions = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_SetOptions");
        D3DPERF_GetStatus = KernelBaseProxy::GetProcAddress_()(dll, "D3DPERF_GetStatus");
    }
} d3d9;

// Hooked exports — go through proxy which may return our wrapper.
// extern "C" gives predictable name decoration so the .def aliases
// match: on x64 the symbol is _Direct3DCreate9Export, on x86 stdcall
// it is __Direct3DCreate9Export@N (compiler-added underscore plus
// arg-byte suffix). Source.def and Source_x86.def encode each variant.
extern "C" {

// Game's static or dynamic d3d9.dll import lands here. Call the
// original via the cached pointer from D3d9Proxy::Init and wrap the
// returned IDirect3D9 in our COM wrapper. The previous
// Direct3DCreate9_Hooked() path re-resolved Direct3DCreate9 through
// HookModule() which, in WorkingMode::D3d9, returned our own module -
// GetProcAddress then handed back this very function and the call
// recursed until the stack ran out.
IDirect3D9* WINAPI _Direct3DCreate9Export(UINT SDKVersion)
{
    LOG_INFO("Direct3DCreate9 called with SDK version: {}", SDKVersion);

    auto orig = D3d9Proxy::Direct3DCreate9_();
    if (orig == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9 not resolved");
        return nullptr;
    }

    auto result = orig(SDKVersion);
    if (result == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9 returned null");
        return nullptr;
    }

    LOG_INFO("Wrapping IDirect3D9 object");
    return new WrappedIDirect3D9Ex(result);
}

HRESULT WINAPI _Direct3DCreate9ExExport(UINT SDKVersion, IDirect3D9Ex** ppD3D)
{
    LOG_INFO("Direct3DCreate9Ex called with SDK version: {}", SDKVersion);

    auto orig = D3d9Proxy::Direct3DCreate9Ex_();
    if (orig == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9Ex not resolved");
        return E_NOTIMPL;
    }

    HRESULT hr = orig(SDKVersion, ppD3D);
    if (FAILED(hr) || ppD3D == nullptr || *ppD3D == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9Ex failed: {:#x}", (unsigned long) hr);
        return hr;
    }

    LOG_INFO("Wrapping IDirect3D9Ex object");
    *ppD3D = new WrappedIDirect3D9Ex(*ppD3D);
    return hr;
}

// PIX/perf passthrough exports
int WINAPI _D3DPERF_BeginEventExport(D3DCOLOR col, LPCWSTR wszName)
{
    if (D3d9Proxy::D3DPERF_BeginEvent_())
        return D3d9Proxy::D3DPERF_BeginEvent_()(col, wszName);
    return 0;
}

int WINAPI _D3DPERF_EndEventExport(void)
{
    if (D3d9Proxy::D3DPERF_EndEvent_())
        return D3d9Proxy::D3DPERF_EndEvent_()();
    return 0;
}

void WINAPI _D3DPERF_SetMarkerExport(D3DCOLOR col, LPCWSTR wszName)
{
    if (D3d9Proxy::D3DPERF_SetMarker_())
        D3d9Proxy::D3DPERF_SetMarker_()(col, wszName);
}

void WINAPI _D3DPERF_SetRegionExport(D3DCOLOR col, LPCWSTR wszName)
{
    if (D3d9Proxy::D3DPERF_SetRegion_())
        D3d9Proxy::D3DPERF_SetRegion_()(col, wszName);
}

BOOL WINAPI _D3DPERF_QueryRepeatFrameExport(void)
{
    if (D3d9Proxy::D3DPERF_QueryRepeatFrame_())
        return D3d9Proxy::D3DPERF_QueryRepeatFrame_()();
    return FALSE;
}

void WINAPI _D3DPERF_SetOptionsExport(DWORD dwOptions)
{
    if (D3d9Proxy::D3DPERF_SetOptions_())
        D3d9Proxy::D3DPERF_SetOptions_()(dwOptions);
}

DWORD WINAPI _D3DPERF_GetStatusExport(void)
{
    if (D3d9Proxy::D3DPERF_GetStatus_())
        return D3d9Proxy::D3DPERF_GetStatus_()();
    return 0;
}

} // extern "C"
