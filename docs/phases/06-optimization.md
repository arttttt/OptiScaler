# Phase 6: Optimization

**Status:** TODO
**Depends on:** Phase 5 (MVP working)

## Goal

Eliminate CPU readback bottleneck, reduce per-frame latency.

## What to do

### D3D9Ex shared handle path for color
Create render targets with `D3DUSAGE_SHARED` directly. Copy backbuffer to shared
surface via `StretchRect` (GPU-only). Eliminates CPU copy for color.

### DX11 intermediate device for depth
Create a DX11 device on the same adapter. `OpenSharedResource` the D3D9Ex depth
surface, then use the existing `IFeature_Dx11wDx12` NT handle pattern to get into
DX12. Eliminates CPU depth readback.

### Async pipeline
Shared fence pattern from `IFeature_Dx11wDx12` (lines 587-643).
DX9Ex lacks fences, but a DX11 device on the same adapter shares the GPU timeline
and can signal/wait. Eliminates CPU GPU-idle wait.

### Depth format conversion on GPU
Replace CPU-side depth format conversion with a DX12 compute shader.
Similar to existing `DepthTransfer_Dx11` in `shaders/depth_transfer/`.

## Expected improvement

| Operation | Before | After |
|-----------|--------|-------|
| Color copy | CPU LockRect + memcpy | GPU StretchRect (shared surface) |
| Depth copy | CPU GetRenderTargetData + LockRect | GPU shared surface + DX11 bridge |
| Sync | CPU wait for GPU idle | GPU fence chain |
| Depth format | CPU loop | DX12 compute shader |

## Verification

- Frame time measurement: before and after optimization
- No visual difference vs MVP
- Test on GPU-limited DX9 game to verify latency reduction
