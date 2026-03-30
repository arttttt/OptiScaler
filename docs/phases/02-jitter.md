# Phase 2: Jitter Injection

**Status:** TODO
**Depends on:** Phase 1

## Goal

Inject Halton(2,3) sub-pixel jitter into the projection matrix each frame.
This is the prerequisite for temporal accumulation in FSR2/XeSS/DLSS.

## What to do

### New file: `misc/HaltonSequence.h`

Extract the Halton function from `inputs/FfxApi_Dx12.cpp:26-34` into a shared header.

Functions needed:
- `float Halton(int32_t index, int32_t base)` — Halton sequence generator
- `void ComputeJitterOffset(int frameIndex, int width, int height, float* outX, float* outY)` — compute per-frame jitter in clip space

Phase count: 32 samples (matches FSR2/DLSS default).

### Modify: `wrapped_d3d9_device.cpp`

**`SetTransform(D3DTS_PROJECTION, ...)`:**
- If `Dx9TAA` config is enabled:
  - Save original matrix in `_originalProjection`
  - Compute Halton jitter: `jittered._31 += jitterX`, `jittered._32 += jitterY`
  - Store `_jitterX`, `_jitterY` for the upscaler
  - Forward modified matrix to real device
- If disabled: pure pass-through (current behavior)

**`BeginScene()`:**
- Increment `_frameIndex` (already stubbed)

### Modify: `Config.h`

Add:
```
CustomOptional<bool> Dx9TAA { false };
```

## Key details

- Jitter in clip space: `2.0f * pixelOffset / viewportDimension`
- DX9 projection matrix element `_31` shifts X, `_32` shifts Y in NDC
- Only `D3DTS_PROJECTION` is modified. View/World pass through unchanged
- Games using vertex shaders instead of `SetTransform` won't get jitter (Phase 7)

## Verification

- Enable `Dx9TAA=true` in OptiScaler.ini
- Image shows visible sub-pixel shimmer on static scenes (expected — no temporal accumulation yet)
- Disabling `Dx9TAA` restores original rendering
