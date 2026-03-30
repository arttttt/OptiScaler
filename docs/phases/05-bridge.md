# Phase 5: DX9→DX12 Bridge and Upscaler Execution

**Status:** TODO
**Depends on:** Phases 2, 3, 4

## Goal

Wire everything together. Bridge DX9 resources to DX12, run FSR2/XeSS/DLSS
in DLAA mode (1:1 ratio), copy result back. This completes the MVP.

## What to do

### New files: `upscalers/IFeature_Dx9wDx12.h/.cpp`

Follow `IFeature_Dx11wDx12` pattern. Responsibilities:
- Create internal DX12 device and command queue
- Manage shared resources between DX9 and DX12
- Handle synchronization
- Invoke upscaler via existing `FeatureProvider_Dx12`

### Resource bridge

**Primary path (D3D9Ex):**
1. Create D3D9Ex texture with `D3DUSAGE_SHARED` flag
2. Open shared handle in DX11 via `ID3D11Device::OpenSharedResource`
3. Get NT handle via `IDXGIResource1::CreateSharedHandle`
4. Open in DX12 via `ID3D12Device::OpenSharedHandle`

**Fallback path (non-Ex):**
1. `GetRenderTargetData` → lockable system memory surface
2. `LockRect` → `memcpy` → DX12 upload heap → `CopyTextureRegion`

### Synchronization

- DX9 side: `IDirect3DQuery9(D3DQUERYTYPE_EVENT)` — issue after last DX9 draw,
  poll `GetData` until GPU is done
- DX12 side: `ID3D12Fence` — signal after upscaler, wait before copying back

### Upscaler execution flow in `Present`

1. Copy backbuffer → shared surface (color)
2. Copy depth buffer (from Phase 3 capture)
3. Flush DX9: issue event query, wait for GPU idle
4. On DX12:
   a. Open shared handles (or upload from CPU staging)
   b. Run MV compute shader (Phase 4)
   c. Populate `NVSDK_NGX_Parameter`: color, depth, MV, jitter offsets
   d. Set render resolution = display resolution (DLAA, ratio 1.0)
   e. Call `Evaluate` on selected upscaler feature
5. Copy output → shared surface
6. `StretchRect` from shared surface → real backbuffer
7. Call real `Present`

### Config additions in `Config.h`

```
CustomOptional<Upscaler, SoftDefault> Dx9Upscaler { Upscaler::FSR22 };
```

## Key details

- All upscaler backends already exist for DX12. No new backend code needed.
- `IFeature_Dx11wDx12` is the direct template — shared texture + fence pattern.
- Quality is auto-detected as DLAA when `renderWidth == displayWidth`.
- AutoExposure is enabled (no exposure texture from game).
- Reactive mask is null (optional, all backends skip it).
- For D3D9Ex shared surfaces to work, the wrapper forces `D3DCREATE_MULTITHREADED`
  in `CreateDevice` (already done in Phase 1).

## Verification

1. Build as d3d9.dll, place next to a DX9 game
2. Set `Dx9TAA=true`, `Dx9Upscaler=fsr22` in OptiScaler.ini
3. Game shows clean temporal AA at native resolution
4. Log shows `FSR2 Evaluate success` (or equivalent)
5. Disabling `Dx9TAA` restores original rendering
6. Test with multiple games to verify stability
