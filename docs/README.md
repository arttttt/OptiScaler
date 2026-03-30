# OptiScaler DX9/DX10 DLAA Support

## Goal

Temporal anti-aliasing (DLAA — FSR2/XeSS/DLSS at 1:1 ratio) for DX9/DX10 games
with zero native upscaler support. The wrapper intercepts rendering, injects jitter,
captures depth, generates motion vectors, bridges resources to DX12, and runs
existing upscaler backends.

## Architecture

```
DX9 Game
  │
  ▼
WrappedIDirect3DDevice9Ex
  ├─ SetTransform(PROJECTION) → jitter injection
  ├─ SetTransform(VIEW/WORLD) → matrix capture for MVs
  ├─ SetDepthStencilSurface   → depth tracking
  ├─ Draw*()                  → pass-through
  └─ Present()
       ├─ capture depth buffer
       ├─ compute ViewProj current/previous
       │
       ▼
     DX9→DX12 Bridge (shared surfaces / CPU staging)
       ├─ color texture  → DX12
       ├─ depth texture  → DX12
       ├─ MV compute shader (depth reprojection)
       │
       ▼
     FSR2 / XeSS / DLSS  (DLAA mode, ratio 1.0)
       │
       ▼
     Result → back to DX9 backbuffer → real Present
```

## Upscaler Backend Compatibility

| Backend | Any GPU? | MV Tolerance | Notes |
|---------|----------|-------------|-------|
| FSR 2.2 | Yes | High | Best for MVP — most forgiving with synthetic MVs |
| FSR 3.1 | Yes | High | Better quality; depth optional with high-res MVs |
| XeSS | Yes (DP4a) | Medium | Best on Intel Arc (XMX) |
| DLSS | NVIDIA only | Lower | Add after MVs are polished |

## Phases

| # | Phase | Status | Doc |
|---|-------|--------|-----|
| 1 | [Foundation — proxy, hooks, COM wrappers](phases/01-foundation.md) | Done | ✅ |
| 2 | [Jitter injection](phases/02-jitter.md) | TODO | ✅ |
| 3 | [Depth buffer capture](phases/03-depth.md) | TODO | ✅ |
| 4 | [Camera motion vector generation](phases/04-motion-vectors.md) | TODO | ✅ |
| 5 | [DX9→DX12 bridge and upscaler execution](phases/05-bridge.md) | TODO | ✅ |
| 6 | [Optimization](phases/06-optimization.md) | TODO | ✅ |
| 7 | [Shader-based projection](phases/07-shader-projection.md) | TODO | ✅ |
| 8 | [Per-object motion vectors](phases/08-per-object-mv.md) | TODO | ✅ |
| 9 | [Menu integration](phases/09-menu.md) | TODO | ✅ |
| 10 | [DX10 support](phases/10-dx10.md) | TODO | ✅ |

## Phase Dependencies

```
1 Foundation ──► 2 Jitter ──────────────────────────┐
                  3 Depth ──────────────────────────┤
                  4 Motion Vectors ─────────────────┤
                                                     ▼
                                              5 Bridge + Upscaler (MVP)
                                                     │
                         ┌───────────────────────────┼───────────────┐
                         ▼                           ▼               ▼
                   6 Optimization          7 Shader Projection   9 Menu
                                                     │
                                                     ▼
                                              8 Per-Object MVs
                                                     │
                                                     ▼
                                              10 DX10 Support
```

Phases 2, 3, 4 can be done in parallel after Phase 1.
Phase 5 requires all of 2, 3, 4.
Phases 6-10 are post-MVP and independent of each other.

## Key Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| D3D9Ex promotion fails for old games | No shared surfaces | CPU staging fallback |
| Missing IDirect3DDevice9 method | Crash | Generated from SDK header, all 134 methods covered |
| Depth format diversity (D16/D24/D32F) | Wrong depth values | Format-specific conversion |
| Shader-based projection (no SetTransform) | No jitter | Phase 7 adds SetVertexShaderConstantF hook |
| Camera-only MVs | Ghosting on moving objects | Phase 8 adds per-object tracking |

## Test Games

| Game | Year | Pipeline | Good for testing |
|------|------|----------|-----------------|
| Half-Life 2 | 2004 | Fixed-function + shaders | Basic DX9, well-known |
| The Elder Scrolls IV: Oblivion | 2006 | Fixed-function | SetTransform jitter path |
| S.T.A.L.K.E.R. | 2007 | Shaders | Shader constant jitter |
| GTA IV | 2008 | Mixed | Complex rendering |
| Fallout 3 / New Vegas | 2008/2010 | Gamebryo | Popular, well-tested |
| Dark Souls: PTDE | 2012 | DX9 shaders | Fixed resolution, good test |

## Config Options

```ini
[DX9]
Dx9TAA=false              ; master enable
Dx9Upscaler=fsr22         ; fsr22, fsr31, xess, dlss
Dx9JitterViaShader=false  ; Phase 7: shader constant path
Dx9ProjectionConstantRegister=-1  ; manual register override
```
