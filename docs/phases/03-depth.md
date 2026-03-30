# Phase 3: Depth Buffer Capture

**Status:** TODO
**Depends on:** Phase 1

## Goal

Capture the scene depth buffer at end-of-frame for the upscaler.

## What to do

### Modify: `wrapped_d3d9_device.cpp`

**`SetDepthStencilSurface(surface)`:**
- Track the surface. Keep reference to the largest depth surface by area
  (same heuristic as ReShade's generic depth addon)
- Match against backbuffer dimensions to avoid shadow maps

**`Present()` / depth capture routine:**
1. Create a lockable offscreen staging surface (once, recreate on size change):
   `CreateOffscreenPlainSurface(width, height, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, ...)`
2. Copy depth via `GetRenderTargetData(depthSurface, stagingSurface)`
3. Convert depth format to R32_FLOAT:
   - `D3DFMT_D16` → `depth / 65535.0f`
   - `D3DFMT_D24X8` / `D3DFMT_D24S8` → `(depth >> 8) / 16777215.0f`
   - `D3DFMT_D32` → `depth / 4294967295.0f`
   - `D3DFMT_D32F_LOCKABLE` → direct float
4. `LockRect` → read the data

### Add to wrapped device state

- `IDirect3DSurface9* _trackedDepthSurface` — current best depth surface
- `IDirect3DSurface9* _depthStagingSurface` — lockable staging copy
- `UINT _bestDepthArea` — area of tracked surface
- `D3DFORMAT _depthFormat` — format of tracked surface

## Key details

- Some games use multiple depth buffers (shadows, reflections, post-processing).
  The largest one matching backbuffer dimensions is usually scene depth.
- `GetRenderTargetData` works for depth surfaces on most drivers. If it fails,
  fallback: hook `Clear(D3DCLEAR_ZBUFFER)` to identify the primary depth buffer.
- MVP uses CPU readback (`LockRect`). Phase 6 replaces this with shared surfaces.
- Depth is captured once per frame in `Present`, before the upscaler runs.

## Verification

- Dump captured depth buffer to a file (e.g., raw R32F or PNG with depth visualization)
- Verify it shows correct scene depth: near objects bright, far objects dark (or inverted)
- Compare against RenderDoc capture of the same scene
