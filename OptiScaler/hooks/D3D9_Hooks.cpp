#include "pch.h"
#include "D3D9_Hooks.h"

#include <proxies/D3D9_Proxy.h>
#include <wrapped/wrapped_d3d9.h>
#include <State.h>

#include <detours/detours.h>

static D3d9Proxy::PFN_Direct3DCreate9 o_Direct3DCreate9 = nullptr;
static D3d9Proxy::PFN_Direct3DCreate9Ex o_Direct3DCreate9Ex = nullptr;

static IDirect3D9* WINAPI hkDirect3DCreate9(UINT SDKVersion)
{
    LOG_FUNC();
    LOG_INFO("Direct3DCreate9 called with SDK version: {}", SDKVersion);

    auto result = o_Direct3DCreate9(SDKVersion);
    if (result == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9 returned null!");
        return nullptr;
    }

    LOG_INFO("Wrapping IDirect3D9 object");
    auto wrapped = new WrappedIDirect3D9Ex(result);
    return wrapped;
}

static HRESULT WINAPI hkDirect3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D)
{
    LOG_FUNC();
    LOG_INFO("Direct3DCreate9Ex called with SDK version: {}", SDKVersion);

    HRESULT hr = o_Direct3DCreate9Ex(SDKVersion, ppD3D);
    if (FAILED(hr) || ppD3D == nullptr || *ppD3D == nullptr)
    {
        LOG_ERROR("Original Direct3DCreate9Ex failed: {:#x}", (unsigned long) hr);
        return hr;
    }

    LOG_INFO("Wrapping IDirect3D9Ex object");
    auto wrapped = new WrappedIDirect3D9Ex(*ppD3D);
    *ppD3D = wrapped;
    return hr;
}

void D3D9Hooks::Hook(HMODULE dx9Module)
{
    LOG_FUNC();

    if (o_Direct3DCreate9 != nullptr)
    {
        LOG_WARN("D3D9 hooks already installed!");
        return;
    }

    D3d9Proxy::Init(dx9Module);

    if (D3d9Proxy::Direct3DCreate9_())
    {
        LOG_INFO("Hooking Direct3DCreate9");
        o_Direct3DCreate9 = D3d9Proxy::Hook_Direct3DCreate9((PVOID) hkDirect3DCreate9);
    }

    if (D3d9Proxy::Direct3DCreate9Ex_())
    {
        LOG_INFO("Hooking Direct3DCreate9Ex");
        o_Direct3DCreate9Ex = D3d9Proxy::Hook_Direct3DCreate9Ex((PVOID) hkDirect3DCreate9Ex);
    }
}

void D3D9Hooks::Unhook()
{
    LOG_FUNC();

    if (o_Direct3DCreate9)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) o_Direct3DCreate9, (PVOID) hkDirect3DCreate9);
        DetourTransactionCommit();
        o_Direct3DCreate9 = nullptr;
    }

    if (o_Direct3DCreate9Ex)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) o_Direct3DCreate9Ex, (PVOID) hkDirect3DCreate9Ex);
        DetourTransactionCommit();
        o_Direct3DCreate9Ex = nullptr;
    }
}
