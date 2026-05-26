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

**Phase count (sequence length):** AMD's formula is `ceil(8 * n²)` where `n`
is the render-to-display scale ratio. For DLAA (1:1, our target) that's
`ceil(8 * 1) = 8`. For reference, FSR2's other quality modes:

| Mode | Scale | Phases |
|------|-------|--------|
| **DLAA** | **1.0x** | **8** |
| Quality | 1.5x | 18 |
| Balanced | 1.7x | 23 |
| Performance | 2.0x | 32 |
| Ultra Performance | 3.0x | 72 |

The `Halton` generator must never return a (0, 0) pair — FSR2 docs require
it. Start `frameIndex` from 1, not 0.

### Modify: `wrapped_d3d9_device.cpp`

**`SetTransform(D3DTS_PROJECTION, ...)`:**
- If `Dx9TAA` config is enabled:
  - Save original matrix in `_originalProjection`
  - Compute Halton jitter in pixel space, then convert to clip space:
    - `clipJitterX = 2.0f * pixelJitterX / viewportWidth`
    - `clipJitterY = -2.0f * pixelJitterY / viewportHeight` *(note: AMD's
      reference uses negative on Y; DX9 Y-direction may flip this — verify
      experimentally before locking in the sign)*
  - Apply: `jittered._31 += clipJitterX; jittered._32 += clipJitterY`
  - Store `_jitterX`, `_jitterY` (pixel-space) for the upscaler later
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
- A single `SetTransform(PROJECTION)` applies to all subsequent draws until
  the game sets a new matrix, so we don't need to re-jitter per draw call
- Games using vertex shaders instead of `SetTransform` won't get jitter (Phase 7)
- Y-axis sign convention may differ between DX9 and the AMD reference
  (which targets DX12); validate by enabling jitter on a static scene and
  watching that the dither appears in both axes evenly

## Verification

- Enable `Dx9TAA=true` in OptiScaler.ini
- Image shows visible sub-pixel shimmer on static scenes (expected — no temporal accumulation yet)
- Disabling `Dx9TAA` restores original rendering
- Toggle camera between still and moving; the dither pattern should be
  pixel-stable when camera is still (i.e., looks like fine noise, not
  smooth animation) and invisible when camera moves naturally

## References

- AMD FSR 2.3.3 docs — Camera jitter:
  https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/
- FidelityFX-FSR2 README (jitter section):
  https://github.com/GPUOpen-Effects/FidelityFX-FSR2/blob/master/README.md
- Existing DX12 reference inside this repo:
  `OptiScaler/inputs/FfxApi_Dx12.cpp:26-34` (Halton function)
- `ffxFsr2GetJitterPhaseCount` / `ffxFsr2GetJitterOffset` in upstream
  FSR2 SDK for the canonical algorithm
