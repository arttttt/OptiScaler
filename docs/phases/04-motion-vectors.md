# Phase 4: Camera Motion Vector Generation

**Status:** TODO
**Depends on:** Phase 1

## Goal

Generate per-pixel motion vectors from camera motion using a DX12 compute shader.
Covers camera rotation, translation, zoom (~70-90% of pixel movement).

## What to do

### New shader: `shaders/motion_vectors/precompile/MV_CameraOnly.hlsl`

Compute shader (16x16 thread groups):
- **Input:** R32F depth texture (SRV), constant buffer with matrices + dimensions
- **Output:** R16G16F motion vector texture (UAV)
- **Algorithm:** For each pixel, reconstruct world position from depth via
  inverse current ViewProjection, reproject through previous ViewProjection,
  compute screen-space delta

Constants:
```
float4x4 InvViewProj_Current
float4x4 ViewProj_Previous
float2   ScreenDimensions
float2   Padding
```

### New files: `shaders/motion_vectors/MV_Dx12.h/.cpp`

Dispatch class following `shaders/depth_transfer/DT_Dx12.h` pattern:
- Root signature: 1 SRV (depth), 1 UAV (MV output), 1 CBV (constants)
- Pipeline state with compute shader
- `Dispatch(cmdList, depthSRV, mvUAV, constants)` method
- Buffer management for the constant buffer

### Modify: `wrapped_d3d9_device.cpp`

- `SetTransform(D3DTS_VIEW, ...)` — save in `_currentView`
- `SetTransform(D3DTS_PROJECTION, ...)` — already saving in `_originalProjection`
- At frame boundary (`Present`):
  ```
  _prevViewProj = _currentViewProj
  _currentViewProj = _currentView * _originalProjection  // use UN-jittered projection
  ```

## Key details

- DX9 uses **row-major** matrices by default. The shader must account for this
  (or transpose on CPU before uploading to constant buffer).
- Use the **original** (un-jittered) projection for MV generation — jitter is for
  temporal accumulation, not for motion estimation.
- Camera-only MVs miss per-object motion: characters, vehicles, particles will
  have incorrect (zero camera-motion) vectors → ghosting on moving objects.
  Acceptable for MVP. Phase 8 improves this.

## Verification

- Visualize MV texture as color (R=horizontal, G=vertical)
- Camera rotation → uniform flow field in the direction of rotation
- Camera still → near-zero MVs everywhere
- Moving objects → MVs show only camera component (expected limitation)
