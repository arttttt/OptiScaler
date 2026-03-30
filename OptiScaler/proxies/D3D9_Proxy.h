#pragma once

#include "SysUtils.h"

#include <State.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <detours/detours.h>

#include <d3d9.h>

class D3d9Proxy
{
  public:
    typedef IDirect3D9*(WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
    typedef HRESULT(WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppD3D);

    typedef int(WINAPI* PFN_D3DPERF_BeginEvent)(D3DCOLOR col, LPCWSTR wszName);
    typedef int(WINAPI* PFN_D3DPERF_EndEvent)(void);
    typedef void(WINAPI* PFN_D3DPERF_SetMarker)(D3DCOLOR col, LPCWSTR wszName);
    typedef void(WINAPI* PFN_D3DPERF_SetRegion)(D3DCOLOR col, LPCWSTR wszName);
    typedef BOOL(WINAPI* PFN_D3DPERF_QueryRepeatFrame)(void);
    typedef void(WINAPI* PFN_D3DPERF_SetOptions)(DWORD dwOptions);
    typedef DWORD(WINAPI* PFN_D3DPERF_GetStatus)(void);

    static void Init(HMODULE module = nullptr)
    {
        if (_dll != nullptr)
            return;

        if (module == nullptr)
        {
            _dll = KernelBaseProxy::GetModuleHandleW_()(L"d3d9.dll");

            if (_dll == nullptr)
                _dll = NtdllProxy::LoadLibraryExW_Ldr(L"d3d9.dll", NULL, 0);
        }
        else
        {
            _dll = module;
        }

        if (_dll == nullptr)
            return;

        _Direct3DCreate9 = (PFN_Direct3DCreate9) KernelBaseProxy::GetProcAddress_()(_dll, "Direct3DCreate9");
        _Direct3DCreate9Ex = (PFN_Direct3DCreate9Ex) KernelBaseProxy::GetProcAddress_()(_dll, "Direct3DCreate9Ex");

        _D3DPERF_BeginEvent = (PFN_D3DPERF_BeginEvent) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_BeginEvent");
        _D3DPERF_EndEvent = (PFN_D3DPERF_EndEvent) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_EndEvent");
        _D3DPERF_SetMarker = (PFN_D3DPERF_SetMarker) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_SetMarker");
        _D3DPERF_SetRegion = (PFN_D3DPERF_SetRegion) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_SetRegion");
        _D3DPERF_QueryRepeatFrame =
            (PFN_D3DPERF_QueryRepeatFrame) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_QueryRepeatFrame");
        _D3DPERF_SetOptions = (PFN_D3DPERF_SetOptions) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_SetOptions");
        _D3DPERF_GetStatus = (PFN_D3DPERF_GetStatus) KernelBaseProxy::GetProcAddress_()(_dll, "D3DPERF_GetStatus");
    }

    static HMODULE Module() { return _dll; }

    // Original methods
    static PFN_Direct3DCreate9 Direct3DCreate9_() { return _Direct3DCreate9; }
    static PFN_Direct3DCreate9Ex Direct3DCreate9Ex_() { return _Direct3DCreate9Ex; }

    static PFN_D3DPERF_BeginEvent D3DPERF_BeginEvent_() { return _D3DPERF_BeginEvent; }
    static PFN_D3DPERF_EndEvent D3DPERF_EndEvent_() { return _D3DPERF_EndEvent; }
    static PFN_D3DPERF_SetMarker D3DPERF_SetMarker_() { return _D3DPERF_SetMarker; }
    static PFN_D3DPERF_SetRegion D3DPERF_SetRegion_() { return _D3DPERF_SetRegion; }
    static PFN_D3DPERF_QueryRepeatFrame D3DPERF_QueryRepeatFrame_() { return _D3DPERF_QueryRepeatFrame; }
    static PFN_D3DPERF_SetOptions D3DPERF_SetOptions_() { return _D3DPERF_SetOptions; }
    static PFN_D3DPERF_GetStatus D3DPERF_GetStatus_() { return _D3DPERF_GetStatus; }

    // Hooked methods (resolve fresh from DLL each time)
    static PFN_Direct3DCreate9 Direct3DCreate9_Hooked()
    {
        return (PFN_Direct3DCreate9) KernelBaseProxy::GetProcAddress_()(HookModule(), "Direct3DCreate9");
    }
    static PFN_Direct3DCreate9Ex Direct3DCreate9Ex_Hooked()
    {
        return (PFN_Direct3DCreate9Ex) KernelBaseProxy::GetProcAddress_()(HookModule(), "Direct3DCreate9Ex");
    }

    // Hook installers
    static PFN_Direct3DCreate9 Hook_Direct3DCreate9(PVOID method)
    {
        auto addr = Direct3DCreate9_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        DetourTransactionCommit();

        return addr;
    }

    static PFN_Direct3DCreate9Ex Hook_Direct3DCreate9Ex(PVOID method)
    {
        auto addr = Direct3DCreate9Ex_ForHook();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        DetourTransactionCommit();

        return addr;
    }

  private:
    inline static HMODULE _dll = nullptr;

    inline static PFN_Direct3DCreate9 _Direct3DCreate9 = nullptr;
    inline static PFN_Direct3DCreate9Ex _Direct3DCreate9Ex = nullptr;

    inline static PFN_D3DPERF_BeginEvent _D3DPERF_BeginEvent = nullptr;
    inline static PFN_D3DPERF_EndEvent _D3DPERF_EndEvent = nullptr;
    inline static PFN_D3DPERF_SetMarker _D3DPERF_SetMarker = nullptr;
    inline static PFN_D3DPERF_SetRegion _D3DPERF_SetRegion = nullptr;
    inline static PFN_D3DPERF_QueryRepeatFrame _D3DPERF_QueryRepeatFrame = nullptr;
    inline static PFN_D3DPERF_SetOptions _D3DPERF_SetOptions = nullptr;
    inline static PFN_D3DPERF_GetStatus _D3DPERF_GetStatus = nullptr;

    inline static HMODULE HookModule()
    {
        if (State::Instance().workingMode == WorkingMode::D3d9)
            return dllModule;

        return _dll;
    }

    static PFN_Direct3DCreate9 Direct3DCreate9_ForHook()
    {
        return (PFN_Direct3DCreate9) KernelBaseProxy::GetProcAddress_()(HookModule(), "Direct3DCreate9");
    }
    static PFN_Direct3DCreate9Ex Direct3DCreate9Ex_ForHook()
    {
        return (PFN_Direct3DCreate9Ex) KernelBaseProxy::GetProcAddress_()(HookModule(), "Direct3DCreate9Ex");
    }
};
