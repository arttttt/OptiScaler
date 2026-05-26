#include "pch.h"

// Empty stub implementations for class statics and free functions whose
// real definitions live in .cpp files excluded from the Win32 build (DX12,
// DX11, Vulkan, upscaler bridges, framegen, menu, etc.). Lets dllmain.cpp
// and hooks/LibraryLoad_Hooks.cpp link in Win32 without dragging in the
// excluded modules. None of these code paths are reachable in DX9-only
// mode at runtime, so empty bodies are safe.

#ifdef _M_IX86

#include <hooks/D3D11_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/Dxgi_Hooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <hooks/Streamline_Hooks.h>
#include <hooks/Kernel_Hooks.h>
#include <spoofing/User32_Spoofing.h>
#include <fsr4/FSR4ModelSelection.h>
#include <fsr4/FSR4Upgrade.h>
#include <misc/IdentifyGpu.h>
#include <nvapi/NvApiTypes.h>
#include <menu/menu_dx12.h>
#include <inputs/FSR2_Dx11.h>
#include <inputs/FSR2_Dx12.h>
#include <inputs/FSR2_Vk.h>
#include <inputs/FSR3_Dx12.h>
#include <inputs/FG/FSR3_Dx12_FG.h>

// hooks/*
void D3D11Hooks::Hook(HMODULE) {}
void D3D12Hooks::Hook() {}
void D3D12Hooks::HookAgility(HMODULE) {}
void DxgiHooks::Hook() {}
void VulkanHooks::Hook(HMODULE) {}

void StreamlineHooks::hookInterposer(HMODULE) {}
void StreamlineHooks::hookDlss(HMODULE) {}
void StreamlineHooks::hookDlssg(HMODULE) {}
void StreamlineHooks::hookReflex(HMODULE) {}
void StreamlineHooks::hookPcl(HMODULE) {}
void StreamlineHooks::hookCommon(HMODULE) {}
bool StreamlineHooks::isInterposerHooked() { return false; }
bool StreamlineHooks::isDlssHooked() { return false; }
bool StreamlineHooks::isDlssgHooked() { return false; }
bool StreamlineHooks::isCommonHooked() { return false; }
bool StreamlineHooks::isPclHooked() { return false; }
bool StreamlineHooks::isReflexHooked() { return false; }

void KernelHooks::Hook() {}
void KernelHooks::HookBase() {}

// spoofing/*
void User32Spoofing::Hook() {}

// fsr4/*
void FSR4ModelSelection::Hook(HMODULE, FSR4Source) {}
void InitFSR4Update() {}
std::vector<std::filesystem::path> GetDriverStore() { return {}; }

// misc/IdentifyGpu
GpuInformation IdentifyGpu::getPrimaryGpu() { return GpuInformation {}; }
std::vector<GpuInformation> IdentifyGpu::getAllGpus() { return {}; }

// nvapi/NvApiTypes
NvApiTypes& NvApiTypes::Instance()
{
    static NvApiTypes instance;
    return instance;
}
unsigned int NvApiTypes::getId(const std::string&) const { return 0; }

// menu/menu_dx12
Menu_Dx12::~Menu_Dx12() {}

// inputs/*
void HookFSR2Inputs(HMODULE) {}
void HookFSR2Dx12Inputs(HMODULE) {}
void HookFSR3Inputs(HMODULE) {}
void HookFSR3Dx12Inputs(HMODULE) {}
void HookFSR2Dx11ExeInputs() {}
void HookFSR2ExeInputs() {}
void HookFSR2VkExeInputs() {}
void HookFSR3ExeInputs() {}

namespace FSR3FG
{
void HookFSR3FGExeInputs() {}
void HookFSR3FGInputs() {}
} // namespace FSR3FG

#endif // _M_IX86
