# Phase 5: DX9→DX11 Bridge and Upscaler Execution

**Status:** 5a (color round-trip) + 5b stand-in (`ClearRenderTargetView`
to red as the visible proof) shipped. FSR2 SDK dispatch is deferred —
the x86 FFX bundle doesn't exist yet, see "Why not FSR2 yet" below.
Pipeline-completeness work (jitter for shader-based games) continues
independently in Phase 7.
**Depends on:** Phases 2, 3, 4

## Why not FSR2 yet

`OptiScaler/library/fsr2/*.lib` ships only x64. The bridge is x86 (every
DX9 game we care about is 32-bit). `optiscaler/FidelityFX-FSR2-DX11`
builds for Win32 from source but needs CI / submodule work to ship an
x86 bundle the way the x64 one is shipped today. Tracked as a separate
task to keep the bridge work moving.

5b uses `ClearRenderTargetView` painting the shared output red as the
visible stand-in. The swap point is one block in `Render`: replace the
`if (Dx9TAA_BridgeDebug) ClearRenderTargetView(...)` branch with the
eventual FSR2 `Evaluate` once the x86 bundle exists.

## Why not the sharpen compute shader

The original 5b plan was a 4-tap unsharp-mask CS writing to a
UAV-bound intermediate texture (`shaders/sharpen/`, still in tree).
`CreateTexture2D` with `DXGI_FORMAT_B8G8R8X8_UNORM` (HL2's backbuffer
format) plus `D3D11_BIND_UNORDERED_ACCESS` killed HL2 hard — that
format isn't in DX11.0's mandatory typed-UAV list, and the D3D11
debug layer's break-on-error policy terminated the host process
before the HRESULT could surface.

When the FFX bundle isn't ready, the right replacement for clear-red
is a Pixel Shader pass writing via RTV (RT bind flag IS inherited
from `D3DUSAGE_RENDERTARGET`, no format gotchas). The Sharpen_Dx11
class stays in tree as the carrier for the cbuffer layout + shader
source the PS rewrite will reuse.

## Current state (5a + 5b stand-in)

The bridge class `IFeature_Dx9wDx11` owns a DX11 device + two shared
color textures (in + out) created from the game's D3D9Ex device, plus
a render-target view on the shared output (only used by the debug
clear path). On every `Present`, when `Dx9TAA` + `Dx9TAA_Bridge` are on:

1. `StretchRect(backbuffer → sharedIn)` (DX9 side)
2. `IDirect3DQuery9(EVENT)` polled to S_OK so DX11 sees finished writes
3. DX11: if `Dx9TAA_BridgeDebug=true` (default),
   `ClearRenderTargetView(sharedOutRtv, RED)` — screen becomes red.
   Otherwise `CopyResource(sharedOut, sharedIn)` for a transparent
   round-trip. `Flush` either way.
4. `StretchRect(sharedOut → backbuffer)` (DX9 side)

The DX11 device is created **without** `D3D11_CREATE_DEVICE_DEBUG` even
in Debug builds. The debug layer issues `__debugbreak()` on errors from
inside the API call, which silently kills the host game (no shell to
trap the break). See the Phase 5b stand-in project memory for the full
post-mortem.

If anything in the chain fails (non-Ex device, shared-handle allocation,
DX11 device creation, `OpenSharedResource`), `_bridgeDisabled` is set
and the wrapper just passes Present through until the next `Reset`.

**Visual outcome:** with `Dx9TAA_BridgeDebug=true`, the whole game
backbuffer comes back red — proof that DX9→DX11→DX9 pixels round-trip
through our DX11 device. Set to `false` for transparent pass-through.
First successful frame logs `Dx9wDx11: first round-trip ok`.

**Known limitation:** non-Ex devices (most pre-2008 titles, possibly
NFSU) bail out at Init. The CPU-readback fallback is a 5b task.

## Goal

Wire everything together. Bridge DX9 resources to DX11, run FSR2/FSR3 in DLAA
mode (1:1 ratio), copy result back. This completes the MVP.

## Why DX11, not DX12

DirectX 12 has no 32-bit Windows runtime. All legacy DX9 games (HL2, NFSU,
Oblivion, STALKER, GTA IV, Dark Souls PTDE, etc.) are 32-bit, so a 32-bit
OptiScaler DLL injected into them cannot load `d3d12.dll`. The bridge target
is forced to DX11.

Consequences:
- The upscaler set shrinks to **FSR 1 / FSR 2 / FSR 3 upscaler**. NVIDIA
  DLSS and Intel XeSS do not ship 32-bit binaries.
- The plumbing is simpler than DX12: DX11.4 has `ID3D11Fence`, so no DX12
  command lists or descriptor heaps are involved.
- `IDirect3D9Ex` shared surfaces map natively to DX11 via
  `ID3D11Device::OpenSharedResource` — no NT handle round-trip.

## What to do

### New files: `upscalers/IFeature_Dx9wDx11.h/.cpp`

Follow the `IFeature_Dx11wDx12` pattern, but with DX9 as the front end and
DX11 as the back end. Responsibilities:

- Own an internal `ID3D11Device` + `ID3D11DeviceContext` on the same adapter
  as the game's DX9 device.
- Manage shared resources between DX9 and DX11.
- Handle synchronization via `IDirect3DQuery9(D3DQUERYTYPE_EVENT)` + DX11
  immediate context flush.
- Hold an `IFeature_Dx11` subclass (e.g. `FSR2FeatureDx11`) and call its
  `Evaluate` each frame.

### Resource bridge

**Primary path (`IDirect3D9Ex` — covers HL2, GTA IV, and any post-2008 game):**

1. Color: create a D3D9Ex render target with `D3DUSAGE_SHARED`. After the
   game renders, `StretchRect` from the real backbuffer into the shared
   surface (GPU-side copy, no CPU readback).
2. Depth: D3D9Ex shared depth requires the INTZ format trick — most games
   don't create depth surfaces that way. For Phase 5 MVP, use CPU readback
   from Phase 3 and upload to a DX11 staging texture; replace with shared
   surfaces in Phase 6.
3. DX11 side: `ID3D11Device::OpenSharedResource(sharedHandle, IID_PPV_ARGS(&tex))`
   gives an `ID3D11Texture2D` backed by the same memory.

**Fallback path (non-Ex `IDirect3D9` — older games and fixed-function-only paths):**

1. `GetRenderTargetData(backbuffer, sysMemSurface)` → `LockRect` → CPU copy.
2. `ID3D11Device::CreateTexture2D` with `D3D11_USAGE_DYNAMIC` + initial data.
3. Per-frame `Map` + `memcpy` for color updates.

The wrapper already detects which path applies (`_realEx != nullptr`).

### Synchronization

- DX9 side: issue `IDirect3DQuery9(D3DQUERYTYPE_EVENT)` after the last DX9
  draw (`EndScene` is a natural point), then poll `GetData(... D3DGETDATA_FLUSH)`
  until the GPU drains.
- DX11 side: after the upscaler `Evaluate`, call `ID3D11DeviceContext::Flush`
  so the result is visible before the readback / shared-surface copy back.
- The two devices live on the same adapter and share the GPU timeline, so no
  cross-device fence is strictly required — flush ordering is enough for MVP.
  Replace with `ID3D11Fence` in Phase 6 to avoid the CPU wait.

### Upscaler execution flow in `Present`

1. Copy backbuffer → shared color surface (`StretchRect` for Ex, CPU for fallback).
2. Copy depth — re-use the staging buffer captured in Phase 3, upload to DX11
   staging texture, then GPU-copy to the upscaler input.
3. Issue DX9 event query, wait for GPU idle.
4. On DX11:
   a. Open shared handles (or update from CPU staging) into `ID3D11Texture2D`.
   b. Run MV compute shader (Phase 4) — produces R16G16F motion vector texture.
   c. Populate `NVSDK_NGX_Parameter`: color, depth, MV, jitter offsets
      (Phase 2 stored these on the wrapper as `_jitterX/_jitterY`).
   d. Render width = display width (DLAA, scale 1.0).
   e. Call `Evaluate` on the selected `IFeature_Dx11` subclass.
5. `ID3D11DeviceContext::Flush`.
6. Copy upscaler output → shared surface → `StretchRect` back to real DX9
   backbuffer.
7. Call real `Present`.

### Selecting the DX11-native backend

The repository already contains DX11-native upscaler features:
- `upscalers/fsr2/FSR2Feature_Dx11.cpp`
- `upscalers/fsr2_212/FSR2Feature_Dx11_212.cpp`
- `upscalers/fsr31/FSR31Feature_Dx11.cpp` (if the FSR3-upscaler-only mode is enabled)

These can be instantiated directly from `IFeature_Dx9wDx11`; no new upscaler
implementation is needed. The dispatch lives behind `Config::Dx9Upscaler`.

### Config additions in `Config.h`

```cpp
// Phase 5: which upscaler the DX9 bridge runs.
// Restricted to DX11-capable backends (DLSS/XeSS are x64-only).
CustomOptional<Upscaler, SoftDefault> Dx9Upscaler { Upscaler::FSR22 };
```

## Key details

- All backend code already exists. The new code is the bridge layer
  (`IFeature_Dx9wDx11`) and the wiring in the wrapped device.
- Quality is auto-detected as DLAA when `renderWidth == displayWidth`.
- AutoExposure is enabled (no exposure texture from game).
- Reactive mask is null (optional, all backends skip it).
- For D3D9Ex shared surfaces to work, the wrapper forces
  `D3DCREATE_MULTITHREADED` in `CreateDevice` (already done in Phase 1).
- The DX11 device should be created on the same DXGI adapter as the game's
  D3D9 device. Use `IDirect3D9Ex::GetAdapterLUID` → match against
  `IDXGIAdapter::GetDesc1::AdapterLuid`.

## Verification

1. Build OptiScaler as 32-bit `d3d9.dll`, place next to a DX9 game (NFSU,
   HL2, etc).
2. `Dx9TAA=true`, `Dx9Upscaler=fsr22` in `OptiScaler.ini`.
3. Game shows clean temporal anti-aliasing at native resolution.
4. Log shows `IFeature_Dx9wDx11::Evaluate ok` (or equivalent).
5. Disabling `Dx9TAA` restores original rendering.
6. Test across the matrix: D3D9Ex game (HL2) and non-Ex game (NFSU); both
   should work via their respective bridge paths.
