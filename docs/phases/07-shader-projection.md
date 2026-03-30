# Phase 7: Shader-Based Projection

**Status:** TODO
**Depends on:** Phase 2 (jitter working for fixed-function)

## Goal

Support DX9 games that set the projection matrix via `SetVertexShaderConstantF`
instead of `SetTransform`. Covers most 2008+ DX9 games.

## What to do

### Modify: `wrapped_d3d9_device.cpp`

**`SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount)`:**
- When `Dx9JitterViaShader` is enabled and `Vector4fCount >= 4`:
  - Check if the data resembles a perspective projection matrix:
    - `_33` and `_34` encode near/far plane (typical pattern: `_34 = -1` or `+1`)
    - `_11` and `_22` encode FOV/aspect ratio (both > 0, typically 0.5-3.0)
    - `_43` != 0 (perspective divide term)
  - If match: apply Halton jitter to `_31` and `_32`
  - Forward modified constants to real device

### Config additions

```
CustomOptional<bool> Dx9JitterViaShader { false };
CustomOptional<int> Dx9ProjectionConstantRegister { -1 }; // -1 = auto-detect
```

When `Dx9ProjectionConstantRegister >= 0`, skip heuristic detection and always
apply jitter to that register range.

## Key details

- Heuristic detection can produce false positives (e.g., shadow projection matrices).
  Per-game testing is essential.
- Some games set the projection matrix multiple times per frame (once per pass).
  Only the main scene pass should get jitter — shadow/reflection passes should not.
  Heuristic: jitter only the matrix set closest to the main draw calls
  (after the last `Clear` with `D3DCLEAR_TARGET`).
- DX9 shader constants use float4 registers. A 4x4 matrix occupies 4 consecutive
  registers (StartRegister to StartRegister+3).

## Verification

- Test with a shader-based DX9 game (e.g., Dark Souls PTDE, S.T.A.L.K.E.R.)
- Verify jitter shimmer appears on static scenes
- Verify shadow maps are NOT affected by jitter
- Compare quality vs fixed-function path on a game that supports both
