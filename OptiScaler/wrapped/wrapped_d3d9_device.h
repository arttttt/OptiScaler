#pragma once

#include <d3d9.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class IFeature_Dx9wDx11;
class WrappedVertexShader9;

// Per-depth-stencil-surface statistics for ReShade-style scene-depth
// identification. We accumulate draw / vertex counts each frame and use them
// at Present time to pick which surface is "the scene depth".
struct DepthSurfaceStats
{
    int vertices = 0;
    int drawcalls = 0;
    int drawcalls_indirect = 0; // DX9 has no real indirect draws, kept for parity
    int last_used_frame = 0;
    UINT width = 0;
    UINT height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    D3DMULTISAMPLE_TYPE multisample = D3DMULTISAMPLE_NONE;
};

class WrappedIDirect3DDevice9Ex final : public IDirect3DDevice9Ex
{
public:
    WrappedIDirect3DDevice9Ex(IDirect3DDevice9* real, IDirect3DDevice9Ex* realEx, HWND hwnd, D3DPRESENT_PARAMETERS* pPresentParams);
    virtual ~WrappedIDirect3DDevice9Ex();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDirect3DDevice9 methods
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override;
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** ppD3D9) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override;
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) override;
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override;
    void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override;
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override;
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) override;
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) override;
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override;
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override;
    HRESULT STDMETHODCALLTYPE Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override;
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) override;
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override;
    void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) override;
    void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override;
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) override;
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) override;
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override;
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override;
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) override;
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) override;
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) override;
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override;
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override;
    HRESULT STDMETHODCALLTYPE EndScene() override;
    HRESULT STDMETHODCALLTYPE Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) override;
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override;
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) override;
    HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9* pViewport) override;
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9* pMaterial) override;
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* pMaterial) override;
    HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, CONST D3DLIGHT9* pLight) override;
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9* pLight) override;
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) override;
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) override;
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, CONST float* pPlane) override;
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) override;
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override;
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** ppSB) override;
    HRESULT STDMETHODCALLTYPE SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus) override;
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* pClipStatus) override;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override;
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) override;
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries) override;
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) override;
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override;
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* PaletteNumber) override;
    HRESULT STDMETHODCALLTYPE SetScissorRect(CONST RECT* pRect) override;
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* pRect) override;
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL bSoftware) override;
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override;
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override;
    float STDMETHODCALLTYPE GetNPatchMode() override;
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override;
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) override;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) override;
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD FVF) override;
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* pFVF) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader) override;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** ppShader) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override;
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override;
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override;
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override;
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) override;
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData) override;
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ppIndexData) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader) override;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** ppShader) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) override;
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo) override;
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo) override;
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override;
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override;

    // IDirect3DDevice9Ex methods
    HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT width, UINT height, float* rows, float* columns) override;
    HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset) override;
    HRESULT STDMETHODCALLTYPE PresentEx(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) override;
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* pPriority) override;
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override;
    HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT iSwapChain) override;
    HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9** pResourceArray, UINT32 NumResources) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND hDestinationWindow) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override;
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage) override;
    HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode) override;
    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) override;

    IDirect3DDevice9* GetReal() { return _real; }

    // Called from WrappedIDirect3D9Ex::CreateDevice (or by Reset) when the
    // auto-depth surface was suppressed in favour of our INTZ. Creates the
    // INTZ texture, binds its level-0 surface, and records it in the INTZ map.
    void InitAutoDepthIntz(UINT width, UINT height);

private:
    void InvalidateTrackedResources();
    void AttemptDepthReadback();
    void ProbeForProjectionMatrix(UINT startRegister, const float* data, UINT vector4fCount);
    void QuerySampleableDepthFormats();
    bool EnsureDepthCopyPS();
    bool EnsureDepthCopyRT(UINT width, UINT height);
    bool EnsureDepthCopyVB(UINT width, UINT height);

    // Phase 3 Mark 2 helpers
    void RegisterDepthSurfaceForStats(IDirect3DSurface9* surface);
    void AccumulateDrawStats(D3DPRIMITIVETYPE primType, UINT primCount, UINT verticesOverride);
    void ResetDepthStatsForCurrentZ();
    void LogTopDepthStats();
    void ReleaseDepthStatsMap();
    void IdentifySceneDepth();
    void ReleaseIntzMap();
    IDirect3DTexture9* IntzTextureFor(IDirect3DSurface9* surface) const;


    IDirect3DDevice9* _real = nullptr;
    IDirect3DDevice9Ex* _realEx = nullptr;
    LONG _refcount = 1;
    HWND _hwnd = nullptr;
    D3DPRESENT_PARAMETERS _presentParams = {};

    // Tracking for later phases
    int _frameIndex = 0;
    D3DMATRIX _currentProjection = {};
    D3DMATRIX _currentView = {};
    D3DMATRIX _currentWorld = {};

    // Phase 5a: DX9→DX11 bridge. Lazy-init on first Present (Dx9TAA on),
    // destroyed in InvalidateTrackedResources so a Reset rebuilds it against
    // the new swap chain dims. Forward-declared in the header to keep DX11
    // out of every translation unit that includes this device wrapper.
    std::unique_ptr<IFeature_Dx9wDx11> _bridge;
    bool _bridgeInitTried = false;
    bool _bridgeDisabled = false;

    // Phase 4: cached current/previous ViewProjection for camera-only MV
    // reprojection. Updated at Present, *after* the frame has finished. The
    // projection stored here is the un-jittered original (Phase 2 only
    // forwards the jittered copy; the member keeps the original). For
    // shader-based games where SetTransform(PROJECTION) never fires both
    // matrices stay at zero — MV gen is a no-op until Phase 7/8 lands.
    D3DMATRIX _currentViewProj = {};
    D3DMATRIX _prevViewProj = {};
    bool _viewProjHasPrev = false;

    // Phase 3: scene depth surface (largest one matching backbuffer dims).
    // AddRef'd while tracked, released on dtor / Reset / replacement.
    IDirect3DSurface9* _trackedDepthSurface = nullptr;
    UINT _trackedDepthArea = 0;
    D3DSURFACE_DESC _trackedDepthDesc = {};
    bool _loggedDepthCapture = false;

    // Phase 3: CPU-side staging copy of the depth surface.
    // Same format / dims as source, D3DPOOL_SYSTEMMEM. Recreated on Reset.
    IDirect3DSurface9* _depthStagingSurface = nullptr;
    // Separate one-shot flags for distinct outcomes — using a single flag let
    // an early "skip" path mask later "success" logs from a different code path.
    bool _loggedReadbackSkip = false;
    bool _loggedReadbackResult = false;

    // Phase 2: sub-pixel jitter applied this frame, in pixel space.
    // Forwarded to the upscaler in later phases.
    float _jitterX = 0.0f;
    float _jitterY = 0.0f;

    // One-shot debug flags: log only the first hit of each path so the
    // log proves the inject ran without spamming every frame.
    bool _loggedProjectionJitter = false;
    bool _loggedIndicatorDraw = false;

    // Phase 7 probe: counts to figure out how the game uses vertex shader constants.
    // Capped logging so we never spam the per-draw hot path.
    int _vsConstCallsThisFrame = 0;
    int _projectionMatchCount = 0;
    bool _loggedFirstFrameVSStats = false;

    // Phase 7a: how many VS bytecode blobs we've already analyzed for oPos
    // writes. Capped to keep logs readable.
    int _vsAnalyzed = 0;

    // Phase 7 Session 1: count of vertex shaders we've wrapped so far.
    // First few wrap events log so we can verify the COM plumbing is alive.
    int _vsWrappedCount = 0;

    // The wrapped vertex shader currently bound to the device, if any.
    // SetVertexShader stores it (AddRef'd via QueryInterface in the hook),
    // GetVertexShader hands it back, dtor / InvalidateTrackedResources
    // release it. nullptr when the game last bound a non-wrapped shader or
    // cleared the VS.
    WrappedVertexShader9* _currentVsWrapper = nullptr;

    // Phase 7 Session 2+: per-shader processing result, keyed by FNV-64 of
    // the original bytecode. The disassemble->assemble (and, from Session 4,
    // transform) is the expensive step; caching by hash means duplicate
    // shaders — HL2 creates the same bytecode more than once — only pay for
    // it once. Vertex shaders survive device Reset (not pool-bound), so the
    // cache is never cleared.
    struct CachedVsResult
    {
        bool usable = false;            // true if `bytecode` is a valid replacement
        bool jittered = false;          // true if the transform injected jitter (vs. plain reassembly)
        std::vector<DWORD> bytecode;    // reassembled (round-trip) / patched (jitter)
        uint32_t jitterConstSlot = 0;   // which cN the patched shader reads jitter from
    };
    std::unordered_map<uint64_t, CachedVsResult> _vsProcessCache;
    int _vsRoundtripLogged = 0;         // cap process-verify log lines
    bool _loggedJitterAsmDump = false;  // dump first shader's disasm+transform once

    // Phase 7 Session 2/3: process one vertex shader (jitter transform when
    // Dx9TAA_VsJitter is on, else plain disasm->asm round-trip), returning
    // the cached result computed on first sight of the hash and reused
    // afterwards. Returns nullptr when processing is disabled or D3DX9 is
    // unavailable.
    const CachedVsResult* ProcessVertexShader(const DWORD* bytecode, size_t dwordLen, uint64_t hash);

    // Phase 7 Session 3: per-frame jitter offset in clip/NDC units (already
    // scaled by Dx9TAA_VsJitterStrength and the Y-flip), recomputed each
    // BeginScene and uploaded to the jitter constant register before each
    // patched-shader draw.
    float _jitterClipX = 0.0f;
    float _jitterClipY = 0.0f;
    // Highest constant register the game itself writes via
    // SetVertexShaderConstantF — tracked so we can warn if it ever reaches
    // our jitter register (a collision that would corrupt the jitter).
    int _gameMaxVsConstReg = -1;
    bool _loggedJitterRegCollision = false;
    // Set once any shader is successfully jittered, so the per-frame upload in
    // BeginScene only touches the jitter register when something reads it.
    bool _anyJitteredShader = false;

    // Phase 3 Mark 2: per-depth-surface activity tracking (ReShade-style).
    // Each surface bound as depth-stencil gets an entry; Draw* methods bump its
    // counters. A separate mutex protects the map because D3D9 with
    // D3DCREATE_MULTITHREADED can hit these from any thread.
    std::mutex _depthStatsMutex;
    std::unordered_map<IDirect3DSurface9*, DepthSurfaceStats> _depthStats;
    IDirect3DSurface9* _currentDepthForStats = nullptr;
    bool _loggedDepthStatsSummary = false;

    // Phase 3 Mark 2: the surface scoring picked as "scene depth" this frame.
    // The map owns the underlying ref; this is a non-owning observer pointer
    // and is nulled in ReleaseDepthStatsMap so it never dangles.
    IDirect3DSurface9* _identifiedSceneDepth = nullptr;
    DepthSurfaceStats _identifiedSceneDepthStats = {};

    // Phase 3 INTZ: which sampleable-depth FourCC formats the adapter supports.
    // Populated once in the constructor via CheckDeviceFormat.
    bool _intzSupported = false;
    bool _rawzSupported = false;
    bool _df24Supported = false;
    bool _df16Supported = false;

    // Phase 3 INTZ: surface -> INTZ texture container map. Every time we
    // substitute an INTZ-backed surface in CreateDepthStencilSurface(Ex), we
    // record the (surface, texture) pair here so the readback path can look
    // up "which texture do I sample for this depth surface". We hold a ref on
    // the texture for as long as the entry lives.
    std::unordered_map<IDirect3DSurface9*, IDirect3DTexture9*> _intzTextureBySurface;
    bool _loggedIntzCreation = false;

    // Phase 3 Mark 2 c4: marks the auto-depth path active for this wrapper.
    // When set, Reset/ResetEx re-runs InitAutoDepthIntz so the bind survives
    // the device losing its surfaces. Cleared when substitution is turned off.
    bool _autoDepthSubstituted = false;

    // Phase 3 INTZ copy pass: R32F render target the depth-copy pixel shader
    // writes to (DEFAULT pool, RENDERTARGET usage). Lazily created on first
    // readback; recreated on Reset.
    IDirect3DTexture9* _depthCopyRT = nullptr;
    IDirect3DSurface9* _depthCopyRTSurface = nullptr;

    // Pixel shader that samples INTZ depth and writes R32F. Compiled at runtime
    // via d3dcompiler_47.dll on first readback.
    IDirect3DPixelShader9* _depthCopyPS = nullptr;
    bool _depthCopyPSFailed = false;

    // Static fullscreen quad geometry (4 pre-transformed vertices, FVF).
    // Recreated on Reset because dimensions are baked in.
    IDirect3DVertexBuffer9* _depthCopyVB = nullptr;
};
