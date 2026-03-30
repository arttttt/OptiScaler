#pragma once

#include "SysUtils.h"

#include <proxies/D3D9_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

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

// Hooked exports — go through proxy which may return our wrapper
IDirect3D9* WINAPI _Direct3DCreate9Export(UINT SDKVersion)
{
    return D3d9Proxy::Direct3DCreate9_Hooked()(SDKVersion);
}

HRESULT WINAPI _Direct3DCreate9ExExport(UINT SDKVersion, IDirect3D9Ex** ppD3D)
{
    return D3d9Proxy::Direct3DCreate9Ex_Hooked()(SDKVersion, ppD3D);
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
