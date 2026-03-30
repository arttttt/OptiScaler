# Phase 8: Per-Object Motion Vectors

**Status:** TODO
**Depends on:** Phase 4 (camera MVs working)

## Goal

Generate per-object MVs from World matrix tracking. Reduces ghosting on
moving characters, vehicles, and other animated objects.

## What to do

### Track per-draw-call World matrices

In `wrapped_d3d9_device.cpp`:
- `SetTransform(D3DTS_WORLD, ...)` — assign an object ID, store
  `(objectID → World_current)` for the current frame
- Before `Present`: swap current → previous tracking tables
- For bone matrices: `SetTransform(D3DTS_WORLDMATRIX(n), ...)` — track similarly

### Object ID render target

Create an additional R32_UINT render target alongside the game's scene.
During draw calls, write the object ID to this target via a simple pixel shader
injected into the pipeline (or via stencil buffer tricks).

### Enhanced MV compute shader

Modify `MV_CameraOnly.hlsl` → `MV_PerObject.hlsl`:
- Input: depth, object ID texture, structured buffer of `(WVP_current, WVP_previous)` pairs
- For each pixel: look up the object's previous WVP, reproject through it

## What this covers

- Camera motion (same as Phase 4)
- Object translation and rotation (rigid body movement)
- Bone/skeletal animation via `D3DTS_WORLDMATRIX(n)` tracking

## What this still misses

- Vertex shader animation (grass, water, cloth) — vertices deformed in shader
- Morph targets — vertices change without matrix change
- Particles — often use shared billboard matrix

## Verification

- Visualize MVs with per-object tracking enabled
- Moving character → MVs show correct motion direction
- Compare ghosting: camera-only vs per-object MVs on a scene with walking NPCs
