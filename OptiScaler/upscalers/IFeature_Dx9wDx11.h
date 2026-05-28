#pragma once
#include <d3d9.h>
#include <d3d11.h>

#include <memory>

class Sharpen_Dx11;
struct Dx9wDx11Fsr2State;

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
    // calls the real Present without intervention. gameDepthR32f is the
    // Phase 3 depth-copy output (R32F render target) or null when no scene
    // depth was identified this frame — it gets bridged to DX11 as the FSR2
    // depth input.
    bool Render(IDirect3DSurface9* gameBackbuffer, IDirect3DSurface9* gameDepthR32f);

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

    // 5b: shared R32F scene depth — the Phase 3 depth-copy output bridged
    // DX9 -> DX11 (same mechanism as color) so FSR2 can read it as its depth
    // input. StretchRect'd from the passed depth surface each Render.
    IDirect3DTexture9* _sharedDepthTex9 = nullptr;
    IDirect3DSurface9* _sharedDepthSurf9 = nullptr;
    HANDLE _sharedDepthHandle = nullptr;
    ID3D11Texture2D* _sharedDepthTex11 = nullptr;
    bool _loggedDepthShare = false;

    // DX9 sync: event query gets issued after StretchRect-in and we poll
    // GetData(D3DGETDATA_FLUSH) before DX11 reads. DX11 flush happens after
    // CopyResource and before DX9 reads back.
    IDirect3DQuery9* _eventQuery = nullptr;

    // 5b unblocked path: instead of running a CS that needs a typed UAV on
    // the backbuffer's format (B8G8R8X8 trips driver/debug-layer asserts on
    // most hardware), the visible-proof mode just ClearRenderTargetView's
    // _sharedOutTex11 with a bright color. RTV on a RENDERTARGET-bound
    // shared texture works on every adapter we care about.
    ID3D11RenderTargetView* _sharedOutRtv = nullptr;

    // 5b: sharpen pass kept around in tree for the PS-based rewrite that
    // will replace the clear-to-red proof. Currently not constructed.
    std::unique_ptr<Sharpen_Dx11> _sharpen;

    // 5b: FSR2 context on the bridge's DX11 device, via the raw ffx_fsr2 API.
    // Pimpl'd (struct defined in the .cpp) so the ffx headers don't leak into
    // every TU that includes this header — same reason _sharpen is
    // forward-declared. We use the raw API rather than FSR2FeatureDx11 because
    // that wrapper drags the whole IFeature / menu / RCAS stack into the
    // 32-bit build. Created at Init (non-fatal), destroyed in ReleaseAll.
    std::unique_ptr<Dx9wDx11Fsr2State> _fsr2;

    bool _loggedRoundtripOk = false;

    bool CreateDx11Device();
    bool CreateSharedColor();
    bool CreateSharedDepth();
    bool InitFsr2();
    bool WaitGpuDx9();
    void ReleaseAll();
};
