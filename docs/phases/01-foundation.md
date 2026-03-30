# Phase 1: Foundation — Proxy, Hooks, COM Wrappers

**Status:** Done
**Commit:** `dbcf203c`

## Goal

OptiScaler loads as `d3d9.dll`, intercepts device creation, wraps `IDirect3D9`
and `IDirect3DDevice9` transparently. Games run unchanged.

## What was done

### New files

| File | Purpose |
|------|---------|
| `proxies/D3D9_Proxy.h` | Loads original d3d9.dll, resolves Direct3DCreate9/Ex and D3DPERF_* |
| `exports/d3d9.h` | Export wrappers that route through proxy |
| `hooks/D3D9_Hooks.h/.cpp` | Hooks Direct3DCreate9/Ex, wraps returned IDirect3D9 |
| `wrapped/wrapped_d3d9.h/.cpp` | IDirect3D9/Ex factory wrapper (~20 methods). CreateDevice wraps device, forces D3DCREATE_MULTITHREADED, sets API::DX9 in State |
| `wrapped/wrapped_d3d9_device.h/.cpp` | IDirect3DDevice9/Ex wrapper (134 methods). All pass-through. Stubs for SetTransform, SetDepthStencilSurface, BeginScene tracking |

### Modified files

| File | Change |
|------|--------|
| `DllNames.h` | `DEFINE_NAME_VECTORS(dx9, "d3d9")` |
| `OptiTypes.h` | `DX9 = 4` in API enum, `Dx9Provider` key |
| `State.h` | `D3d9` WorkingMode, `currentD3D9Device`, `d3d9Devices` vector, `#include <d3d9.h>` |
| `Source.def` | D3D9 exports (ordinals 200-208) |
| `dllmain.cpp` | d3d9.dll detection in CheckWorkingMode(), loads proxy + original library |
| `LibraryLoad_Hooks.cpp` | d3d9 interception in LoadLibraryCheckW |
| `exports/exports.h` | `#include "d3d9.h"` |

## Patterns followed

- **Proxy:** `D3D12_Proxy.h` — static class, lazy load, Detours hooks
- **Exports:** `exports/d3d12.h` — struct with FARPROC, LoadOriginalLibrary
- **Hooks:** `D3D11_Hooks.cpp` — hook global creation function, wrap result
- **Factory wrapper:** `wrapped_factory.h` — COM wrapper, QueryInterface routing
- **Device wrapper:** `wrapped_swapchain.h` — full interface implementation

## Verification

1. Build OptiScaler as d3d9.dll
2. Rename and place next to a DX9 game
3. Game runs normally, no crashes
4. OptiScaler.log shows `WorkingMode: D3d9` and `Wrapping IDirect3DDevice9`
