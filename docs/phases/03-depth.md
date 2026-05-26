# Phase 3: Depth Buffer Capture

**Status:** Implemented (Mark 3), HL2 verified, per-game opt-out for
incompatible titles
**Depends on:** Phase 1

## Goal

Make the scene depth buffer sampleable so the upscaler (or any
read-back code) can get per-pixel depth at the end of a frame.

## Why the obvious approach doesn't work

A first reading of the D3D9 API suggests "use `GetRenderTargetData`
to copy the depth surface into a `D3DPOOL_SYSTEMMEM` staging surface
and `LockRect` the result." This fails on real hardware:

- `CreateOffscreenPlainSurface` refuses to allocate `D3DFMT_D24S8` /
  `D3DFMT_D24X8` surfaces in `D3DPOOL_SYSTEMMEM`
  (`D3DERR_INVALIDCALL`). The system-memory staging path is closed
  for depth formats.
- `GetRenderTargetData` from a depth surface to a regular RT is also
  unsupported in vanilla D3D9.

The well-known workaround is the **INTZ trick**: replace the depth
surface with a texture created with the FourCC format `'INTZ'` which
is bindable as both depth-stencil *and* sampleable texture. Once we
have a sampleable depth texture, a fullscreen pixel-shader pass
copies depth into an R32F render target, and `GetRenderTargetData`
on the R32F target into a system-memory R32F staging works normally.

INTZ has been a NVIDIA capability since G80 (2006); AMD's HD2000
(2007) and Intel modern GPUs expose it too. ReShade uses the same
trick.

## Architecture

The wrapper implements ReShade-style depth surface tracking and an
INTZ substitution path:

### Per-surface activity tracking

Every surface bound as depth-stencil gets an entry in
`_depthStats: surface → { vertices, drawcalls, last_used_frame,
dims, format }`. The four `Draw*` overrides bump the current
surface's counters; `Clear(D3DCLEAR_ZBUFFER)` resets stats for the
currently-bound depth (end-of-scene boundary).

The map is mutex-protected because we force `D3DCREATE_MULTITHREADED`
on every wrapped device.

### Scene depth identification

At each `Present`, `IdentifySceneDepth()` scores every tracked
surface using ReShade's heuristic:

- `preferDrawcalls = (drawcalls_indirect >= drawcalls / 3)`
  — never trips on D3D9 (no native indirect-draw API), so the
  effective rule is "max vertices wins."
- Filters: vertices > 3 (skip blits / UI), last_used_frame within 2
  of current (skip abandoned surfaces), aspect ratio within ±10% of
  backbuffer (skip shadow maps and cube faces).

The winner is stored in `_identifiedSceneDepth`. The chosen surface
drives the readback path.

### INTZ substitution

`CreateDepthStencilSurface` / `CreateDepthStencilSurfaceEx` are
intercepted. If the requested surface matches the backbuffer
dimensions, is non-MSAA, and is a D24 format (D24S8 or D24X8), we
substitute:

1. `CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, INTZ,
   D3DPOOL_DEFAULT, ...)`
2. `GetSurfaceLevel(0, ...)` to get the surface backed by the INTZ
   texture
3. Wrap that surface in `WrappedDepthStencilSurface9` (see
   below) and return the wrapper to the game

Both the texture and the wrapper are stored in
`_intzTextureBySurface` so the readback path can sample the
correct INTZ for the identified scene-depth surface.

### Auto-depth substitution

Most DX9 games (NFSU among them) never call
`CreateDepthStencilSurface` manually — they ask D3D9 to create an
implicit auto-depth via `EnableAutoDepthStencil = TRUE` in the
present params. To capture depth in those games, the wrapper
suppresses `EnableAutoDepthStencil` in
`WrappedIDirect3D9Ex::CreateDevice` (saving the original params for
caller retries), and after the device is created calls
`InitAutoDepthIntz` to allocate our INTZ texture, bind its surface
via `SetDepthStencilSurface`, and seed the same tracking + map state
the explicit path produces.

`Reset` and `ResetEx` invalidate every default-pool resource. The
wrapper remembers `_autoDepthSubstituted` so it can re-run
`InitAutoDepthIntz` after a successful reset.

### Format-lying proxy (`WrappedDepthStencilSurface9`)

Several games (Oblivion, Fallout NV, NFSU — the comment in
`reshade/source/d3d9/d3d9_resource.cpp` calls these out
specifically) verify the format of depth surfaces they get back and
fail rendering / crash if they see anything other than what they
asked for. Returning a raw INTZ surface breaks them.

The wrapper class `WrappedDepthStencilSurface9` (ported from
ReShade's `Direct3DDepthStencilSurface9`, BSD-3-Clause) forwards
every `IDirect3DSurface9` method to the real INTZ surface, except
`GetDesc` returns a cached "original" descriptor (D24S8 etc.). The
game sees the format it asked for; the underlying surface is INTZ.

`SetDepthStencilSurface` overrides on the wrapped device unwrap the
proxy via `QueryInterface` for the class's UUID before forwarding to
the real device (which has no knowledge of our wrappers).
`GetDepthStencilSurface` does the inverse — scans the INTZ map and
substitutes the proxy for any raw INTZ surface the real device
returns.

### Copy pass

`AttemptDepthReadback()` runs once per `Present`:

1. Pick the source surface — prefer `_identifiedSceneDepth`
   (Mark 2 scoring), fall back to the area-tracked surface if
   identification has no result yet.
2. `IntzTextureFor(source)` — lookup the INTZ texture backing that
   surface in our map. Skip if no INTZ entry (the surface wasn't
   substituted — usually means non-D24 format or MSAA).
3. `CreateStateBlock(D3DSBT_ALL)` captures every render state.
4. Set up the copy pass: R32F texture's level-0 surface as render
   target, depth-stencil unbound, our ps_2_0 (compiled lazily via
   `d3dcompiler_47.dll`) as pixel shader, INTZ texture as sampler 0,
   pre-transformed fullscreen quad (XYZRHW FVF).
5. `DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2)`.
6. State block `Apply()` restores the game's pipeline.
7. `CreateOffscreenPlainSurface(R32F, SYSTEMMEM)` for the staging
   surface (R32F is allowed in SYSTEMMEM, unlike D24S8).
8. `GetRenderTargetData(R32F RT → R32F SYSTEMMEM)`.
9. `LockRect` reads the centre-pixel value as a one-shot sanity log.

## Configuration

`Dx9TAA_AutoDepthSubstitution` (default `true`) — mirrors ReShade's
`s_disable_intz` opt-out. When set to `false`, the wrapper stops
suppressing auto-depth in `CreateDevice`. The rest of the depth
pipeline still runs, but games whose only depth is the auto-depth
won't have INTZ available for sampling.

**Use this knob when the game artifacts / crashes with the default.**
Verified-broken: NFSU (Renderware engine, 2003). Likely-affected
class: other 2003-era Renderware titles, anything that depends on
specific stencil-op semantics.

## Verification

The log shows the full chain:

```
Sampleable-depth format INTZ: SUPPORTED (hr=0x00000000)
Auto-depth suppressed for INTZ substitution (orig format=0x4B)
Auto-depth INTZ initialised and bound (1920x1080, proxy reports D24S8 to game)
Phase 3 Mark 2: top 1 depth surfaces by vertex count after first activity:
  #1 surface=0x... 1920x1080 fmt=0x4B MS=0 draws=N verts=K lastFrame=1
Identified scene depth surface: 0x... 1920x1080 fmt=0x4B MS=0 draws=N verts=K
INTZ depth-copy pixel shader ready (ps_2_0)
INTZ depth-copy R32F RT ready (1920x1080)
INTZ depth readback OK (1920x1080) — center pixel depth = 0.847
```

The centre-pixel depth in `[0, 1]` proves the entire INTZ → R32F →
SYSTEMMEM round-trip works. HL2 in the main menu reports `1.0` (sky
at the centre), which is correct.

## Known limitations

- **MSAA depth surfaces are skipped.** `GetRenderTargetData` needs
  resolved sources; we don't implement the resolve step yet.
- **NFSU and similar titles crash with INTZ substitution.** No
  in-tree fix; use the `Dx9TAA_AutoDepthSubstitution=false`
  opt-out. See [project memory](../README.md) for the failure mode
  details.
- **Refcount edge cases.** ReShade's auto-depth wrapper has a
  special-case for D3D9's `_ref=0` start convention (the implicit
  reference D3D9 holds on auto-depth). Our wrapper drives `_orig`
  refs in lockstep with its own. Star-Wars-TFU-style double-release
  bugs may surface; not yet observed.

## Mark history

- **Mark 1**: naive INTZ substitution in CreateDepthStencilSurface
  only. Worked for games that explicitly create depth (rare). HL2
  caught a non-scene depth surface; NFSU never triggered.
- **Mark 2**: ReShade-style per-surface scoring + multi-INTZ map +
  scene identification at Present time + auto-depth substitution.
  Auto-depth crashed NFSU; reverted that piece, kept scoring.
- **Mark 3**: Re-added auto-depth substitution via the format-lying
  proxy. NFSU still crashed for non-format reasons. Added the
  config opt-out. Final shipped state.

## References

- [INTZ format documentation (aras-p)](https://aras-p.info/texts/D3D9GPUHacks.html#depth)
- [ReShade Direct3DDepthStencilSurface9](https://github.com/crosire/reshade/blob/main/source/d3d9/d3d9_resource.cpp#L113) — the format-lying proxy we ported
- [ReShade generic_depth_addon — scoring heuristic](https://github.com/crosire/reshade/blob/main/examples/09-depth/generic_depth_addon.cpp) — `draw_stats::operator>` and `s_disable_intz`
