#pragma once
#include <d3d9.h>
#include <d3d11.h>

#include <memory>

class Sharpen_Dx11;

// DX9 → DX11 bridge for the DLAA upscaler path. 32-bit only: DX12 has no
// x86 runtime and every DX9 game we care about is x86.
//
// Phase 5a (current): shared color round-trip only — backbuffer goes DX9 →
// shared surface → DX11 → shared surface → DX9 backbuffer. DX11 just does a
// CopyResource on the shared textures. The trip is a smoke test that the
// device pair / shared resource handles / sync all work on the user's
// hardware. The visible output is unchanged.
//
// Phase 5b (next): replace the CopyResource with FSR2Feature_Dx11::Evaluate
// fed by the depth from Phase 3, MV from Phase 4, and jitter from Phase 2.
//
// Shared color requires IDirect3DDevice9Ex. Non-Ex devices (older games that
// never upgraded via QueryInterface) will fall back in Phase 5b to a CPU
// readback path; 5a fails Init for non-Ex and the wrapper skips bridging.
class IFeature_Dx9wDx11
{
  public:
    // Both ctor and dtor live in the .cpp on purpose — _sharpen is a
    // std::unique_ptr<Sharpen_Dx11> and Sharpen_Dx11 is forward-declared
    // here, so inline (= default) definitions would force every TU that
    // instantiates this class to also see the full Sharpen_Dx11 header.
    IFeature_Dx9wDx11();
    ~IFeature_Dx9wDx11();

    bool Init(IDirect3DDevice9* gameDevice, IDirect3DDevice9Ex* gameDeviceEx, UINT width, UINT height,
              D3DFORMAT format);

    // Round-trip the game's backbuffer through DX11. Returns false on any
    // failure; caller treats failure as "no bridge this frame" and just
    // calls the real Present without intervention.
    bool Render(IDirect3DSurface9* gameBackbuffer);

    bool IsInit() const { return _init; }
    UINT Width() const { return _width; }
    UINT Height() const { return _height; }

  private:
    bool _init = false;

    // Non-owning game-side refs. The wrapped device owns the actual D3D9
    // pointers; we hold raw references for the lifetime of the bridge.
    IDirect3DDevice9* _gameDevice = nullptr;
    IDirect3DDevice9Ex* _gameDeviceEx = nullptr;

    UINT _width = 0;
    UINT _height = 0;
    D3DFORMAT _gameFormat = D3DFMT_UNKNOWN;

    // DX11 device. Default adapter for now — for multi-GPU setups Phase 5b
    // matches the game's adapter LUID via IDXGIFactory + IDirect3D9Ex::
    // GetAdapterLUID.
    ID3D11Device* _dx11Device = nullptr;
    ID3D11DeviceContext* _dx11Context = nullptr;

    // Shared color input: DX9 texture (D3DUSAGE_RENDERTARGET + shared
    // handle) and the same memory exposed as a DX11 ID3D11Texture2D. The
    // game backbuffer gets StretchRect'd into the DX9 side.
    IDirect3DTexture9* _sharedInTex9 = nullptr;
    IDirect3DSurface9* _sharedInSurf9 = nullptr;
    HANDLE _sharedInHandle = nullptr;
    ID3D11Texture2D* _sharedInTex11 = nullptr;

    // Same as _sharedIn*, but for the path back: DX11 writes into this; DX9
    // StretchRect'd back into the real backbuffer.
    IDirect3DTexture9* _sharedOutTex9 = nullptr;
    IDirect3DSurface9* _sharedOutSurf9 = nullptr;
    HANDLE _sharedOutHandle = nullptr;
    ID3D11Texture2D* _sharedOutTex11 = nullptr;

    // DX9 sync: event query gets issued after StretchRect-in and we poll
    // GetData(D3DGETDATA_FLUSH) before DX11 reads. DX11 flush happens after
    // CopyResource and before DX9 reads back.
    IDirect3DQuery9* _eventQuery = nullptr;

    // 5b: optional sharpen pass. Replaces the 5a CopyResource — runs a
    // tiny unsharp-mask CS into its own UAV-bound output, which we then
    // CopyResource into _sharedOutTex11 (shared resources can't be UAVs
    // because DX9 has no equivalent bind flag). Owned by unique_ptr so
    // including the full Sharpen_Dx11 header stays out of consumers.
    std::unique_ptr<Sharpen_Dx11> _sharpen;

    bool _loggedRoundtripOk = false;

    bool CreateDx11Device();
    bool CreateSharedColor();
    bool WaitGpuDx9();
    void ReleaseAll();
};
