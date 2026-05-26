# Phase 4: Camera Motion Vector Generation

**Status:** Standalone dispatcher + HLSL + view/proj capture done.
Bridge wiring is the FSR2 hookup in Phase 5b. **Output is zero on
shader-based games** (HL2, NFSU, every non-fixed-function title) until
Phase 7 lands matrix capture via vertex-shader bytecode patching — the
same root cause as the dead [Phase 2](02-jitter.md) `SetTransform` path.
**Depends on:** Phase 1

## Goal

Generate per-pixel motion vectors from camera motion using a DX11 compute
shader. Covers camera rotation, translation, zoom (~70-90% of pixel
movement). Per-object motion is Phase 8.

## Why DX11

Per [Phase 5](05-bridge.md), the DX9 bridge target is DX11, not DX12 — DX12
has no 32-bit runtime and every legacy DX9 game is 32-bit. The MV compute
runs on the DX11 device owned by `IFeature_Dx9wDx11`. The HLSL shader is
identical to a DX12 version; only the dispatcher class differs.

## What to do

### New shader: `shaders/motion_vectors/precompile/MV_CameraOnly.hlsl`

Compute shader (16x16 thread groups):

- **Input:** R32F depth texture (SRV), constant buffer with matrices +
  dimensions
- **Output:** R16G16F motion vector texture (UAV)
- **Algorithm:** For each pixel, reconstruct world position from depth via
  inverse current ViewProjection, reproject through previous ViewProjection,
  compute screen-space delta

Constants:

```hlsl
cbuffer MVConstants : register(b0)
{
    float4x4 InvViewProj_Current;
    float4x4 ViewProj_Previous;
    float2   ScreenDimensions;
    float2   Padding;
};
```

### New files: `shaders/motion_vectors/MV_Dx11.h/.cpp`

Dispatch class following the existing
`shaders/depth_transfer/DT_Dx11.h` pattern:

- `ID3D11ComputeShader* _computeShader`
- `ID3D11Buffer* _constantBuffer` (size `sizeof(MVConstants)`, aligned to 256)
- `ID3D11ShaderResourceView* _srvDepth`
- `ID3D11UnorderedAccessView* _uavMv`
- `bool CreateBufferResource(ID3D11Device*, UINT width, UINT height)` — creates
  the R16G16F output texture and the views
- `bool Dispatch(ID3D11DeviceContext*, ID3D11Texture2D* depthTex, const MVConstants&)`
  — uploads constants, binds SRV/UAV, calls `Dispatch(ceil(W/16), ceil(H/16), 1)`,
  unbinds (to release UAV for the next stage)

Construct as `std::unique_ptr<MV_Dx11>` inside the `IFeature_Dx9wDx11` bridge.

### Modify: `wrapped_d3d9_device.cpp`

- `SetTransform(D3DTS_VIEW, ...)` — already saving in `_currentView`
- `SetTransform(D3DTS_PROJECTION, ...)` — already saving in `_currentProjection`
  (un-jittered original — Phase 2 stores the jittered copy only as the
  forwarded argument, the member keeps the original)
- At frame boundary (`Present`):
  ```cpp
  _prevViewProj = _currentViewProj;
  D3DXMatrixMultiply(&_currentViewProj, &_currentView, &_currentProjection);
  ```
- The bridge reads `_prevViewProj` and `_currentViewProj` plus the inverse of
  the current one when populating `MVConstants`.

## Key details

- DX9 uses **row-major** matrices by default. HLSL defaults to column-major;
  either transpose on the CPU before uploading or annotate the cbuffer with
  `row_major`. Match what the rest of the OptiScaler shader code does
  (check `shaders/depth_transfer/`).
- Use the **original (un-jittered)** projection for MV generation. Jitter is
  for the upscaler's temporal accumulation, not for motion estimation —
  feeding the jittered projection here produces a sub-pixel "jitter motion"
  that ruins reprojection.
- Camera-only MVs miss per-object motion: characters, vehicles, particles
  will have only the camera component → ghosting on moving objects.
  Acceptable for MVP. Phase 8 improves this via shader-constant inspection.
- Depth input comes from the DX11 staging texture uploaded in Phase 5
  (which in turn comes from Phase 3's CPU readback). Phase 6 replaces this
  CPU hop with a shared-resource path.

## Verification

- Visualize the MV texture as color (R = horizontal motion, G = vertical).
- Camera rotation → uniform flow field in the direction of rotation.
- Camera still → near-zero MVs everywhere.
- Moving objects → MVs show only the camera component (expected limitation).
- Sanity check the magnitudes: MV values are in **NDC space per frame**
  (range roughly [-1, +1] for large camera rotations); FSR2 expects either
  NDC or UV-space and the convention is fixed per upscaler — verify which
  one our `IFeature_Dx11` subclass expects before locking in.
