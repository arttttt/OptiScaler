# Phase 9: Menu Integration

**Status:** TODO
**Depends on:** Phase 5 (MVP working)

## Goal

ImGui overlay for DX9 with status display and settings.

## What to do

### Hook `EndScene` for ImGui rendering

Use ImGui's DX9 backend (`imgui_impl_dx9.h`, already in the project's ImGui includes).
Follow the pattern of `menu_overlay_dx.h` / `menu_dx11.h`.

### Display DX9-specific status

- DLAA enabled/disabled
- Selected upscaler backend and its status
- Jitter mode: fixed-function / shader constants / disabled
- Depth capture: active / format / resolution
- MV generation: camera-only / per-object
- Frame time breakdown: DX9 render / depth copy / MV generation / upscaler / copy back

### Settings exposed in menu

- Enable/disable Dx9TAA
- Select upscaler backend
- Toggle jitter path
- Debug overlays: depth buffer visualization, MV visualization

## Verification

- Press INSERT (default hotkey) → menu appears
- Settings changes apply in real-time
- Menu renders correctly without visual artifacts
- Menu works in both windowed and fullscreen modes
