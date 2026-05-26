#pragma once

#include <d3d9.h>
#include <cstdint>
#include <vector>

// Wrapper around IDirect3DVertexShader9 used by Phase 7 jitter injection.
//
// The wrapper holds the original shader plus (lazily) a patched variant.
// CreateVertexShader returns a wrapper to the game; SetVertexShader on our
// device unwraps to forward _real or _patched into the underlying device.
// All other game-visible behaviour (GetDevice, GetFunction, ref counting)
// proxies to the original shader so games observing the shader handle
// can't tell anything was substituted.
//
// Pattern is the same as WrappedDepthStencilSurface9 from Phase 3 (which
// in turn was ported from ReShade). Session 1 of Phase 7 ships this
// wrapper as a no-op proxy — _patched stays null, _state stays Untried —
// so we can validate the COM plumbing in isolation before adding the
// disassemble/transform/assemble pipeline in Sessions 2-4.
//
// Architectural reference for the wrap-then-defer pattern: 3DMigoto
// DirectX9/HookedVertexShader.cpp + Globals.h::OriginalShaderInfo. We
// don't port their code verbatim because their hooking framework is
// generated and tightly coupled to their globals; the pattern itself is
// what we follow.
class __declspec(uuid("7B3F2A0C-1D4E-4B5F-8A6C-9E2F3B4D5E6F"))
    WrappedVertexShader9 final : public IDirect3DVertexShader9
{
  public:
    enum class PatchState : uint8_t
    {
        Untried = 0,    // Session 1 default — patcher not yet attempted
        PatchedOk,      // Session 4+ — _patched is the bytecode the device sees
        PatchFailed,    // disasm/transform/asm failed; always use _real
    };

    WrappedVertexShader9(IDirect3DVertexShader9* real, const DWORD* origBytecode, size_t bytecodeDwordLen,
                         uint64_t fnv64);
    ~WrappedVertexShader9();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDirect3DVertexShader9
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void* pData, UINT* pSizeOfData) override;

    // Picks which underlying shader the device should actually use — the
    // patched variant once Session 4 wires it in, otherwise the original.
    IDirect3DVertexShader9* EffectiveShader() const { return _patched ? _patched : _real; }

    IDirect3DVertexShader9* RealShader() const     { return _real; }
    IDirect3DVertexShader9* PatchedShader() const  { return _patched; }
    PatchState State() const                       { return _state; }
    uint64_t Hash() const                          { return _hash; }
    const std::vector<DWORD>& OrigBytecode() const { return _origBytecode; }
    uint32_t JitterConstSlot() const               { return _jitterConstSlot; }

    // Session 4 calls these once the patcher has produced a valid shader.
    // Wrapper takes ownership of `patched`; releases it in its dtor.
    void SetPatched(IDirect3DVertexShader9* patched, uint32_t jitterConstSlot);
    void MarkPatchFailed();

  private:
    IDirect3DVertexShader9* _real = nullptr;
    IDirect3DVertexShader9* _patched = nullptr;
    LONG _refcount = 1;

    PatchState _state = PatchState::Untried;
    uint32_t _jitterConstSlot = 0;

    std::vector<DWORD> _origBytecode;
    uint64_t _hash = 0;
};
