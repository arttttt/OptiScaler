# Phase 7: Jitter for Shader-Based Games (VS bytecode patching)

**Status:** Core proven on HL2 (2026-05-29). Jitter injection works; calibration
(strength 1.0, Y-sign, UI scoping) is intertwined with the resolve (Phase 5b).
**Depends on:** Phase 1 (wrapper). Supersedes Phase 2, which is dead on
shader-era games.

## Why this exists

Phase 2 jitters the projection matrix in `SetTransform(D3DTS_PROJECTION)`. That
hook never fires on shader-era games (HL2, NFSU, anything 2003+): they compute
clip-space position in a vertex shader from constants they upload via
`SetVertexShaderConstantF`, never touching the fixed-function transform. So jitter
has to go *into the shader*.

An earlier plan (see git history of this file) tried to detect the projection
matrix heuristically inside `SetVertexShaderConstantF` and offset it. That's
fragile — the matrix register differs per shader, games upload several matrices
(shadow/reflection passes), and detection produces false positives. We took the
robust route instead: **patch the vertex shader bytecode** so it applies the
jitter itself, reading the per-frame offset from a constant register we drive.

This is the technique HelixMod / 3DMigoto use for D3D9 stereo fixes.

## How it works (shipped)

Pipeline, all behind `Dx9TAA_VsJitter` (default on):

1. **Wrap** every `IDirect3DVertexShader9` in `WrappedVertexShader9`
   (`wrapped/wrapped_d3d9_vertexshader.{h,cpp}`). The wrapper holds the original
   shader plus a lazily-built patched variant; `SetVertexShader` unwraps to the
   real device, `GetVertexShader` hands the wrapper back. (Session 1.)

2. **Disassemble** the bytecode with `D3DXDisassembleShader`, **reassemble** with
   `D3DXAssembleShader`, both dynamically loaded from `d3dx9_43.dll`
   (`misc/dxbc/D3DX9Shader.{h,cpp}`). The reassembled-but-unmodified round-trip
   was verified bit-identical at the instruction level first. (Session 2.)

3. **Transform** the disassembly text (`Dxbc_VsPatcher::BuildJitteredAsm`):
   - register analysis (max temp / max const / position writes) comes from the
     ported DXVK decoder (`misc/dxbc/dxso_decoder.{h,cpp}`, `AnalyzeRegisterUsage`)
     — reliable numbers, not text scraping;
   - pick a free temp `rN` = maxTemp+1 (fail if it exceeds the SM temp limit);
   - redirect every write to the position output into `rN`. The output is `oPos`
     for vs_2_0 (named, undeclared) or the `dcl_position oN` register for vs_3_0
     (the one beginning with `o` — `dcl_position vN` is the *input*, getting this
     wrong was the first build's bug);
   - append `mad rN.xy, c<reg>.xy, rN.w, rN.xy` then `mov <pos>, rN`. The `* w`
     keeps the post-divide NDC shift constant in pixels. Output registers are
     write-only in SM2/SM3, which is why the value must stage through a temp.
   (Session 3.)

4. **Upload** the per-frame jitter to `c<reg>` (`BeginScene` computes it with the
   8-phase Halton sequence; `SetVertexShader` re-uploads per bind). The patched
   `mad` reads it. (Session 4.)

Results are cached by FNV-64 of the bytecode (`_vsProcessCache`) so duplicate
shaders disassemble/transform once. A shader that can't be patched (no free temp,
already references `c<reg>`, disasm/asm/CreateVertexShader fails) falls back to the
original — jitter is simply skipped for it.

## The constant register

`Dx9TAA_VsJitterRegister` (default **c250**). HelixMod reserves c200–c250 the same
way — near the top of the 256-register file. The catch found on HL2: Source
**bulk-writes the entire constant file** (`cModel[53]` alone spans ~c58–c216, and
the game touches up through c255), so `SetVertexShaderConstantF` tracking reports
"no free register". But *written ≠ read*: the identity test (strength 0, c250
clobbered to zero every frame) rendered HL2 bit-for-bit identical, proving no
shader actually reads c250. So clobbering it is safe despite the collision
warning. Games that genuinely read the top of the file would need a different
register — the warning + the `MaybeDumpVsConstUsage` log exist to catch that.

## Config

```
Dx9TAA_VsJitter          { true }   // enable the transform + bind + upload
Dx9TAA_VsJitterRegister  { 250 }    // constant register carrying the jitter
Dx9TAA_VsJitterStrength  { 1.0 }    // 0 = identity test; 1 = sub-pixel; >1 = exaggerated
Dx9TAA_VsRoundtripTest   { false }  // fallback: reassemble without transform
```

## Verified

- HL2 (vs_2_0): all shaders patch and the driver accepts them; identity test
  (strength 0) bit-identical; visible uniform wobble at strength 8 → jitter
  reaches the shader and is depth-correct (constant screen-space offset).

## Open / deferred to resolve integration (Phase 5b)

- **Y-sign**: `clipY` is negated (AMD DX12 reference). Only matters once a resolve
  consumes the jitter — verify against FSR2's expected convention then.
- **UI/HUD scoping**: we currently jitter *every* vertex shader, so 2D/UI wobbles
  too. The scene jitter must be paired with a resolve that un-jitters via motion
  vectors; UI is composited after upscaling. Scoping out UI shaders (pre-transformed
  position / orthographic / specific draws) is part of wiring the real resolve.
- **vs_3_0**: transform handles it (dcl_position o-register), but untested — HL2 is
  all vs_2_0. Validate on a vs_3_0 title.
- **Per-game register**: if a game reads the top of the constant file, expose
  `Dx9TAA_VsJitterRegister` per-game (the collision warning flags this).
