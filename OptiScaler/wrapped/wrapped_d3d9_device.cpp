#include <pch.h>
#include "wrapped_d3d9_device.h"

#include "misc/HaltonSequence.h"

WrappedIDirect3DDevice9Ex::WrappedIDirect3DDevice9Ex(IDirect3DDevice9* real, IDirect3DDevice9Ex* realEx, HWND hwnd, D3DPRESENT_PARAMETERS* pPresentParams)
    : _real(real), _realEx(realEx), _hwnd(hwnd)
{
    if (pPresentParams)
        _presentParams = *pPresentParams;

    ZeroMemory(&_currentProjection, sizeof(D3DMATRIX));
    ZeroMemory(&_currentView, sizeof(D3DMATRIX));
    ZeroMemory(&_currentWorld, sizeof(D3DMATRIX));

    QuerySampleableDepthFormats();
}

WrappedIDirect3DDevice9Ex::~WrappedIDirect3DDevice9Ex()
{
    InvalidateTrackedResources();

    if (_depthCopyPS)
    {
        _depthCopyPS->Release();
        _depthCopyPS = nullptr;
    }
}

// Phase 3 INTZ c3: lazy-compile the pixel shader that samples an INTZ texture
// and writes the depth value to an R32F render target. Uses D3DCompile from
// d3dcompiler_47.dll loaded dynamically, so we don't have to link against it.
// Sets _depthCopyPSFailed on compile failure to avoid retrying every frame.
bool WrappedIDirect3DDevice9Ex::EnsureDepthCopyPS()
{
    if (_depthCopyPS)
        return true;
    if (_depthCopyPSFailed)
        return false;

    static const char* kHLSL =
        "sampler2D depthSampler : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0\n"
        "{\n"
        "    return float4(tex2D(depthSampler, uv).x, 0, 0, 1);\n"
        "}\n";

    HMODULE d3dcompilerDll = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dcompilerDll)
    {
        _depthCopyPSFailed = true;
        LOG_ERROR("d3dcompiler_47.dll not loadable — INTZ depth-copy disabled");
        return false;
    }

    using D3DCompile_pfn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*,
                                            LPCSTR, LPCSTR, UINT, UINT,
                                            ID3DBlob**, ID3DBlob**);
    auto D3DCompile_fn = reinterpret_cast<D3DCompile_pfn>(GetProcAddress(d3dcompilerDll, "D3DCompile"));
    if (!D3DCompile_fn)
    {
        FreeLibrary(d3dcompilerDll);
        _depthCopyPSFailed = true;
        LOG_ERROR("D3DCompile entry point missing — INTZ depth-copy disabled");
        return false;
    }

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile_fn(kHLSL, strlen(kHLSL), nullptr, nullptr, nullptr,
                                     "main", "ps_2_0", 0, 0, &code, &errors);

    if (FAILED(hr) || code == nullptr)
    {
        if (errors != nullptr)
        {
            LOG_ERROR("Depth-copy PS compile failed: {}",
                      static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        else
        {
            LOG_ERROR("Depth-copy PS compile failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
        if (code) code->Release();
        FreeLibrary(d3dcompilerDll);
        _depthCopyPSFailed = true;
        return false;
    }

    IDirect3DPixelShader9* ps = nullptr;
    const HRESULT createHr = _real->CreatePixelShader(
        static_cast<const DWORD*>(code->GetBufferPointer()), &ps);

    code->Release();
    if (errors) errors->Release();
    FreeLibrary(d3dcompilerDll);

    if (FAILED(createHr) || ps == nullptr)
    {
        LOG_ERROR("CreatePixelShader for depth-copy failed: hr=0x{:08X}",
                  static_cast<uint32_t>(createHr));
        _depthCopyPSFailed = true;
        return false;
    }

    _depthCopyPS = ps;
    LOG_INFO("INTZ depth-copy pixel shader ready (ps_2_0)");
    return true;
}

// Phase 3 INTZ c3: lazy-create the R32F render target the copy pass writes to.
// Sized to match the depth surface; recreated on Reset via InvalidateTrackedResources.
bool WrappedIDirect3DDevice9Ex::EnsureDepthCopyRT(UINT width, UINT height)
{
    if (_depthCopyRT && _depthCopyRTSurface)
        return true;

    const HRESULT hr = _real->CreateTexture(
        width, height, 1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F,
        D3DPOOL_DEFAULT,
        &_depthCopyRT,
        nullptr);

    if (FAILED(hr) || _depthCopyRT == nullptr)
    {
        LOG_ERROR("Depth-copy R32F RT CreateTexture failed: hr=0x{:08X}",
                  static_cast<uint32_t>(hr));
        return false;
    }

    const HRESULT slHr = _depthCopyRT->GetSurfaceLevel(0, &_depthCopyRTSurface);
    if (FAILED(slHr) || _depthCopyRTSurface == nullptr)
    {
        LOG_ERROR("Depth-copy RT GetSurfaceLevel failed: hr=0x{:08X}",
                  static_cast<uint32_t>(slHr));
        _depthCopyRT->Release();
        _depthCopyRT = nullptr;
        return false;
    }

    LOG_INFO("INTZ depth-copy R32F RT ready ({}x{})", width, height);
    return true;
}

// Phase 3 INTZ c3: build a static pre-transformed (XYZRHW) quad covering the
// full target. Pre-transformed means no vertex shader is needed; we render
// directly in pixel coordinates. Half-pixel offset matches the DX9 texel/pixel
// rasterisation rule so the sampled depth lines up 1:1 with the source.
bool WrappedIDirect3DDevice9Ex::EnsureDepthCopyVB(UINT width, UINT height)
{
    if (_depthCopyVB)
        return true;

    struct QuadVertex
    {
        float x, y, z, rhw;
        float u, v;
    };

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const QuadVertex verts[4] = {
        { -0.5f,       -0.5f,       0.0f, 1.0f, 0.0f, 0.0f },
        {  w - 0.5f,   -0.5f,       0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f,        h - 0.5f,   0.0f, 1.0f, 0.0f, 1.0f },
        {  w - 0.5f,    h - 0.5f,   0.0f, 1.0f, 1.0f, 1.0f },
    };

    const HRESULT hr = _real->CreateVertexBuffer(
        sizeof(verts),
        D3DUSAGE_WRITEONLY,
        D3DFVF_XYZRHW | D3DFVF_TEX1,
        D3DPOOL_DEFAULT,
        &_depthCopyVB,
        nullptr);

    if (FAILED(hr) || _depthCopyVB == nullptr)
    {
        LOG_ERROR("Depth-copy VB CreateVertexBuffer failed: hr=0x{:08X}",
                  static_cast<uint32_t>(hr));
        return false;
    }

    void* dst = nullptr;
    const HRESULT lockHr = _depthCopyVB->Lock(0, 0, &dst, 0);
    if (FAILED(lockHr) || dst == nullptr)
    {
        LOG_ERROR("Depth-copy VB Lock failed: hr=0x{:08X}", static_cast<uint32_t>(lockHr));
        _depthCopyVB->Release();
        _depthCopyVB = nullptr;
        return false;
    }

    memcpy(dst, verts, sizeof(verts));
    _depthCopyVB->Unlock();

    return true;
}

// Phase 3 INTZ c1: ask the driver which sampleable-depth FourCC formats it
// supports. INTZ is the modern one (D24S8 contents readable as a single-channel
// texture); RAWZ / DF24 / DF16 are vendor / legacy variants kept here for
// diagnostics in case INTZ is missing.
//
// AdapterFormat must be the actual display-mode format. Many games create
// devices in windowed mode with BackBufferFormat=D3DFMT_UNKNOWN, in which
// case passing it here yields false negatives for every FourCC. We pull the
// real adapter format via GetAdapterDisplayMode instead.
void WrappedIDirect3DDevice9Ex::QuerySampleableDepthFormats()
{
    IDirect3D9* d3d9 = nullptr;
    if (FAILED(_real->GetDirect3D(&d3d9)) || d3d9 == nullptr)
    {
        LOG_WARN("QuerySampleableDepthFormats: GetDirect3D returned null");
        return;
    }

    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (FAILED(_real->GetCreationParameters(&params)))
    {
        LOG_WARN("QuerySampleableDepthFormats: GetCreationParameters failed");
        d3d9->Release();
        return;
    }

    D3DDISPLAYMODE mode = {};
    const HRESULT modeHr = d3d9->GetAdapterDisplayMode(params.AdapterOrdinal, &mode);
    if (FAILED(modeHr))
    {
        LOG_WARN("GetAdapterDisplayMode failed: hr=0x{:08X} — falling back to BackBufferFormat=0x{:X}",
                 static_cast<uint32_t>(modeHr),
                 static_cast<uint32_t>(_presentParams.BackBufferFormat));
        mode.Format = _presentParams.BackBufferFormat;
    }

    LOG_INFO("Querying sampleable-depth formats with adapter={}, devType={}, displayFormat=0x{:X}",
             params.AdapterOrdinal,
             static_cast<int>(params.DeviceType),
             static_cast<uint32_t>(mode.Format));

    auto check = [&](const char* name, D3DFORMAT fmt) -> bool {
        const HRESULT hr = d3d9->CheckDeviceFormat(
            params.AdapterOrdinal,
            params.DeviceType,
            mode.Format,
            D3DUSAGE_DEPTHSTENCIL,
            D3DRTYPE_SURFACE,
            fmt);
        const bool ok = SUCCEEDED(hr);
        LOG_INFO("Sampleable-depth format {}: {} (hr=0x{:08X})", name,
                 ok ? "SUPPORTED" : "not supported",
                 static_cast<uint32_t>(hr));
        return ok;
    };

    _intzSupported = check("INTZ", static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')));
    _rawzSupported = check("RAWZ", static_cast<D3DFORMAT>(MAKEFOURCC('R', 'A', 'W', 'Z')));
    _df24Supported = check("DF24", static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '2', '4')));
    _df16Supported = check("DF16", static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '1', '6')));

    d3d9->Release();
}

// Phase 3 Mark 2 helpers ====================================================

static int VerticesFromPrimitiveCount(D3DPRIMITIVETYPE primType, UINT primCount)
{
    switch (primType)
    {
    case D3DPT_POINTLIST:     return static_cast<int>(primCount);
    case D3DPT_LINELIST:      return static_cast<int>(primCount * 2);
    case D3DPT_LINESTRIP:     return static_cast<int>(primCount + 1);
    case D3DPT_TRIANGLELIST:  return static_cast<int>(primCount * 3);
    case D3DPT_TRIANGLESTRIP: return static_cast<int>(primCount + 2);
    case D3DPT_TRIANGLEFAN:   return static_cast<int>(primCount + 2);
    default:                  return 0;
    }
}

void WrappedIDirect3DDevice9Ex::RegisterDepthSurfaceForStats(IDirect3DSurface9* surface)
{
    if (surface == nullptr)
    {
        _currentDepthForStats = nullptr;
        return;
    }

    std::lock_guard<std::mutex> lock(_depthStatsMutex);
    auto it = _depthStats.find(surface);
    if (it == _depthStats.end())
    {
        DepthSurfaceStats stats = {};
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(surface->GetDesc(&desc)))
        {
            stats.width = desc.Width;
            stats.height = desc.Height;
            stats.format = desc.Format;
            stats.multisample = desc.MultiSampleType;
        }
        surface->AddRef();
        _depthStats.emplace(surface, stats);
    }
    _currentDepthForStats = surface;
}

void WrappedIDirect3DDevice9Ex::AccumulateDrawStats(D3DPRIMITIVETYPE primType, UINT primCount, UINT verticesOverride)
{
    if (_currentDepthForStats == nullptr)
        return;

    const int verts = verticesOverride > 0
                          ? static_cast<int>(verticesOverride)
                          : VerticesFromPrimitiveCount(primType, primCount);

    std::lock_guard<std::mutex> lock(_depthStatsMutex);
    auto it = _depthStats.find(_currentDepthForStats);
    if (it == _depthStats.end())
        return;

    it->second.vertices += verts;
    it->second.drawcalls += 1;
    it->second.last_used_frame = _frameIndex;
}

void WrappedIDirect3DDevice9Ex::ResetDepthStatsForCurrentZ()
{
    if (_currentDepthForStats == nullptr)
        return;

    std::lock_guard<std::mutex> lock(_depthStatsMutex);
    auto it = _depthStats.find(_currentDepthForStats);
    if (it == _depthStats.end())
        return;

    it->second.vertices = 0;
    it->second.drawcalls = 0;
    it->second.drawcalls_indirect = 0;
}

void WrappedIDirect3DDevice9Ex::LogTopDepthStats()
{
    if (_loggedDepthStatsSummary)
        return;

    std::lock_guard<std::mutex> lock(_depthStatsMutex);

    struct Sample { IDirect3DSurface9* surface; DepthSurfaceStats stats; };
    std::vector<Sample> samples;
    samples.reserve(_depthStats.size());
    for (const auto& kv : _depthStats)
    {
        if (kv.second.vertices > 0)
            samples.push_back({ kv.first, kv.second });
    }
    if (samples.empty())
        return;

    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
        return a.stats.vertices > b.stats.vertices;
    });

    _loggedDepthStatsSummary = true;

    const size_t n = std::min<size_t>(samples.size(), 3);
    LOG_INFO("Phase 3 Mark 2: top {} depth surfaces by vertex count after first activity:", n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto& s = samples[i];
        LOG_INFO("  #{} surface={} {}x{} fmt=0x{:X} MS={} draws={} verts={} lastFrame={}",
                 i + 1, static_cast<const void*>(s.surface),
                 s.stats.width, s.stats.height,
                 static_cast<uint32_t>(s.stats.format),
                 static_cast<int>(s.stats.multisample),
                 s.stats.drawcalls, s.stats.vertices, s.stats.last_used_frame);
    }
}

void WrappedIDirect3DDevice9Ex::ReleaseDepthStatsMap()
{
    std::lock_guard<std::mutex> lock(_depthStatsMutex);
    for (auto& kv : _depthStats)
    {
        if (kv.first)
            kv.first->Release();
    }
    _depthStats.clear();
    _currentDepthForStats = nullptr;
    _identifiedSceneDepth = nullptr; // observer ptr, the map held the ref
    _loggedDepthStatsSummary = false;
}

void WrappedIDirect3DDevice9Ex::ReleaseIntzMap()
{
    for (auto& kv : _intzTextureBySurface)
    {
        if (kv.second)
            kv.second->Release();
    }
    _intzTextureBySurface.clear();
}

IDirect3DTexture9* WrappedIDirect3DDevice9Ex::IntzTextureFor(IDirect3DSurface9* surface) const
{
    if (!surface)
        return nullptr;
    auto it = _intzTextureBySurface.find(surface);
    return it != _intzTextureBySurface.end() ? it->second : nullptr;
}

// Phase 3 Mark 2 c4: build and bind an INTZ-backed surface as the device's
// depth-stencil after WrappedIDirect3D9Ex::CreateDevice suppressed auto-depth.
// Re-run from Reset/ResetEx if _autoDepthSubstituted is still set, since the
// device drops every default-pool surface on reset.
void WrappedIDirect3DDevice9Ex::InitAutoDepthIntz(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        LOG_ERROR("InitAutoDepthIntz: invalid dims {}x{}", width, height);
        return;
    }

    const D3DFORMAT intzFormat = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));

    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = _real->CreateTexture(
        width, height, 1,
        D3DUSAGE_DEPTHSTENCIL,
        intzFormat,
        D3DPOOL_DEFAULT,
        &tex,
        nullptr);
    if (FAILED(hr) || tex == nullptr)
    {
        LOG_ERROR("Auto-depth INTZ CreateTexture failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    IDirect3DSurface9* surf = nullptr;
    hr = tex->GetSurfaceLevel(0, &surf);
    if (FAILED(hr) || surf == nullptr)
    {
        LOG_ERROR("Auto-depth INTZ GetSurfaceLevel failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        tex->Release();
        return;
    }

    hr = _real->SetDepthStencilSurface(surf);
    if (FAILED(hr))
    {
        LOG_ERROR("Auto-depth INTZ SetDepthStencilSurface failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        surf->Release();
        tex->Release();
        return;
    }

    // Add to map so the readback path can find which INTZ texture backs this
    // surface. The map owns the texture ref; the surface ref is shared with
    // the device's internal binding plus our _trackedDepthSurface seat.
    _intzTextureBySurface[surf] = tex;

    if (_trackedDepthSurface)
        _trackedDepthSurface->Release();
    _trackedDepthSurface = surf;
    _trackedDepthArea = width * height;
    _trackedDepthDesc = {};
    _trackedDepthDesc.Width = width;
    _trackedDepthDesc.Height = height;
    _trackedDepthDesc.Format = intzFormat;
    _trackedDepthDesc.MultiSampleType = D3DMULTISAMPLE_NONE;

    _loggedDepthCapture = true;   // suppress redundant SetDepthStencilSurface log
    _loggedIntzCreation = true;   // suppress redundant intercept log
    _loggedReadbackSkip = false;
    _loggedReadbackResult = false;

    _autoDepthSubstituted = true; // makes Reset re-run us on the new device state

    LOG_INFO("Auto-depth INTZ initialised and bound ({}x{})", width, height);
}

// Phase 3 Mark 2 c2: score every tracked depth surface against the ReShade
// heuristic and pick the winner. Called once per Present.
//
//   prefer_drawcalls = (drawcalls_indirect >= drawcalls / 3)
//   winner = max-by-(drawcalls if prefer_drawcalls else vertices)
//
// DX9 has no real indirect-draw API, so prefer_drawcalls will essentially
// never trip — vertices wins in practice. Filters:
//   - vertices > 3 (suppresses fullscreen-quad blits, UI passes)
//   - used within the last 2 frames (drops abandoned surfaces)
//   - aspect ratio within 10% of backbuffer (drops shadow maps / cube faces)
void WrappedIDirect3DDevice9Ex::IdentifySceneDepth()
{
    if (_presentParams.BackBufferWidth == 0 || _presentParams.BackBufferHeight == 0)
        return;

    const float bbAspect = static_cast<float>(_presentParams.BackBufferWidth) /
                           static_cast<float>(_presentParams.BackBufferHeight);
    constexpr float kAspectTolerance = 0.10f;

    std::lock_guard<std::mutex> lock(_depthStatsMutex);

    IDirect3DSurface9* winner = nullptr;
    DepthSurfaceStats winnerStats = {};

    for (const auto& kv : _depthStats)
    {
        const auto& s = kv.second;

        if (s.vertices <= 3) continue;
        if (_frameIndex - s.last_used_frame > 2) continue;
        if (s.width == 0 || s.height == 0) continue;

        const float surfaceAspect = static_cast<float>(s.width) / static_cast<float>(s.height);
        const float aspectDelta = std::fabs(surfaceAspect - bbAspect) / bbAspect;
        if (aspectDelta > kAspectTolerance) continue;

        const bool preferDrawcalls = (s.drawcalls > 0 && s.drawcalls_indirect >= s.drawcalls / 3);
        bool wins;
        if (winner == nullptr)
        {
            wins = true;
        }
        else if (preferDrawcalls)
        {
            wins = s.drawcalls > winnerStats.drawcalls;
        }
        else
        {
            wins = s.vertices > winnerStats.vertices;
        }

        if (wins)
        {
            winner = kv.first;
            winnerStats = s;
        }
    }

    if (winner != _identifiedSceneDepth)
    {
        IDirect3DSurface9* prev = _identifiedSceneDepth;
        _identifiedSceneDepth = winner;
        _identifiedSceneDepthStats = winner ? winnerStats : DepthSurfaceStats{};

        // Re-arm the readback gates so the next attempt reports the outcome
        // against the new surface.
        _loggedReadbackSkip = false;
        _loggedReadbackResult = false;

        if (winner)
        {
            LOG_INFO("Identified scene depth surface: {} {}x{} fmt=0x{:X} MS={} draws={} verts={}",
                     static_cast<const void*>(winner),
                     winnerStats.width, winnerStats.height,
                     static_cast<uint32_t>(winnerStats.format),
                     static_cast<int>(winnerStats.multisample),
                     winnerStats.drawcalls, winnerStats.vertices);
        }
        else if (prev)
        {
            LOG_INFO("Lost previously identified scene depth surface (no candidate this frame)");
        }
    }
    else if (winner)
    {
        // Refresh cached stats even when the surface is unchanged — width may
        // be the same but draw counts evolve frame-to-frame.
        _identifiedSceneDepthStats = winnerStats;
    }
}

void WrappedIDirect3DDevice9Ex::InvalidateTrackedResources()
{
    ReleaseDepthStatsMap();

    if (_trackedDepthSurface)
    {
        _trackedDepthSurface->Release();
        _trackedDepthSurface = nullptr;
    }
    _trackedDepthArea = 0;
    _trackedDepthDesc = {};
    _loggedDepthCapture = false;

    if (_depthStagingSurface)
    {
        _depthStagingSurface->Release();
        _depthStagingSurface = nullptr;
    }
    _loggedReadbackSkip = false;
    _loggedReadbackResult = false;

    ReleaseIntzMap();
    _identifiedSceneDepthStats = {};
    _loggedIntzCreation = false;

    if (_depthCopyRTSurface)
    {
        _depthCopyRTSurface->Release();
        _depthCopyRTSurface = nullptr;
    }
    if (_depthCopyRT)
    {
        _depthCopyRT->Release();
        _depthCopyRT = nullptr;
    }
    if (_depthCopyVB)
    {
        _depthCopyVB->Release();
        _depthCopyVB = nullptr;
    }
    // _depthCopyPS survives Reset — it's a compiled pixel shader, no GPU resources
    // tied to surface dims. Only freed in the destructor.
}

// Phase 7 probe: scan a vertex-shader constant-buffer upload for a 4x4 block
// that looks like a perspective projection matrix. Log up to 5 hits, then go
// quiet to avoid spamming the per-draw hot path.
//
// DX9 row-major perspective:  m[2][3] == 1, m[3][3] == 0
// Column-major (transposed):  m[3][2] == 1, m[3][3] == 0
// Both cases require non-zero m00/m11 (focal / aspect terms).
void WrappedIDirect3DDevice9Ex::ProbeForProjectionMatrix(UINT startRegister, const float* data, UINT vector4fCount)
{
    if (_projectionMatchCount >= 5)
        return;

    constexpr float kEps = 1e-3f;

    for (UINT i = 0; i + 4 <= vector4fCount; i += 4)
    {
        const float* m = data + i * 4;

        const float m00 = m[0];
        const float m11 = m[5];
        const float m22 = m[10];
        const float m33 = m[15];
        const float m23 = m[11];
        const float m32 = m[14];

        const bool basic =
            std::fabs(m00) > kEps &&
            std::fabs(m11) > kEps &&
            std::fabs(m33) < kEps;

        const bool rowMaj = basic && std::fabs(m23 - 1.0f) < kEps;
        const bool colMaj = basic && std::fabs(m32 - 1.0f) < kEps;

        if (rowMaj || colMaj)
        {
            _projectionMatchCount++;
            LOG_INFO("Phase 7 probe: possible {} projection at vs c{}-c{} (m00={:.3f}, m11={:.3f}, m22={:.3f})",
                     rowMaj ? "row-major" : "col-major",
                     startRegister + i,
                     startRegister + i + 3,
                     m00, m11, m22);

            if (_projectionMatchCount >= 5)
            {
                LOG_INFO("Phase 7 probe: hit log cap (5), further matches will be silent");
                return;
            }
        }
    }
}

void WrappedIDirect3DDevice9Ex::AttemptDepthReadback()
{
    // Phase 3 Mark 2 c5: prefer the surface identified by ReShade-style
    // scoring; fall back to Mark 1's area-based tracker if no scoring result
    // is available yet (first few frames before activity accumulates).
    IDirect3DSurface9* sourceSurface = _identifiedSceneDepth ? _identifiedSceneDepth : _trackedDepthSurface;
    UINT width = 0, height = 0;
    D3DFORMAT sourceFormat = D3DFMT_UNKNOWN;
    D3DMULTISAMPLE_TYPE sourceMS = D3DMULTISAMPLE_NONE;

    if (_identifiedSceneDepth)
    {
        width = _identifiedSceneDepthStats.width;
        height = _identifiedSceneDepthStats.height;
        sourceFormat = _identifiedSceneDepthStats.format;
        sourceMS = _identifiedSceneDepthStats.multisample;
    }
    else if (_trackedDepthSurface)
    {
        width = _trackedDepthDesc.Width;
        height = _trackedDepthDesc.Height;
        sourceFormat = _trackedDepthDesc.Format;
        sourceMS = _trackedDepthDesc.MultiSampleType;
    }
    else
    {
        return;
    }

    if (sourceMS != D3DMULTISAMPLE_NONE)
    {
        if (!_loggedReadbackSkip)
        {
            _loggedReadbackSkip = true;
            LOG_WARN("Depth readback skipped: MSAA depth surface ({}x{} samples={})",
                     width, height, static_cast<int>(sourceMS));
        }
        return;
    }

    // Without an INTZ-backed texture for this surface we have nothing to sample.
    IDirect3DTexture9* intzTex = IntzTextureFor(sourceSurface);
    if (!intzTex)
    {
        if (!_loggedReadbackSkip)
        {
            _loggedReadbackSkip = true;
            LOG_WARN("Depth readback skipped: source surface {} is not INTZ-backed (format=0x{:X})",
                     static_cast<const void*>(sourceSurface),
                     static_cast<uint32_t>(sourceFormat));
        }
        return;
    }

    if (!EnsureDepthCopyPS() || !EnsureDepthCopyRT(width, height) || !EnsureDepthCopyVB(width, height))
        return; // helpers already logged the specific failure

    // Capture every render state so the game sees no side effects after we draw.
    IDirect3DStateBlock9* savedState = nullptr;
    HRESULT hr = _real->CreateStateBlock(D3DSBT_ALL, &savedState);
    if (FAILED(hr) || savedState == nullptr)
    {
        if (!_loggedReadbackResult)
        {
            _loggedReadbackResult = true;
            LOG_ERROR("CreateStateBlock failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
        return;
    }

    // Set up the copy pass: R32F as RT, depth disabled, our PS + INTZ as sampler 0.
    _real->SetRenderTarget(0, _depthCopyRTSurface);
    _real->SetDepthStencilSurface(nullptr);

    _real->SetVertexShader(nullptr);
    _real->SetPixelShader(_depthCopyPS);
    _real->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    _real->SetStreamSource(0, _depthCopyVB, 0, sizeof(float) * 6);

    _real->SetTexture(0, intzTex);
    _real->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    _real->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    _real->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    _real->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    _real->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    _real->SetRenderState(D3DRS_LIGHTING, FALSE);
    _real->SetRenderState(D3DRS_ZENABLE, FALSE);
    _real->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    _real->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    _real->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    _real->SetRenderState(D3DRS_FOGENABLE, FALSE);
    _real->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    _real->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

    const HRESULT drawHr = _real->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    // Restore everything regardless of draw outcome.
    savedState->Apply();
    savedState->Release();

    if (FAILED(drawHr))
    {
        if (!_loggedReadbackResult)
        {
            _loggedReadbackResult = true;
            LOG_ERROR("Depth-copy DrawPrimitive failed: hr=0x{:08X}", static_cast<uint32_t>(drawHr));
        }
        return;
    }

    // Now stage R32F -> SYSTEMMEM. CreateOffscreenPlainSurface accepts R32F
    // (unlike D24S8) so this part works.
    if (!_depthStagingSurface)
    {
        const HRESULT stagingHr = _real->CreateOffscreenPlainSurface(
            width, height,
            D3DFMT_R32F,
            D3DPOOL_SYSTEMMEM,
            &_depthStagingSurface,
            nullptr);

        if (FAILED(stagingHr))
        {
            if (!_loggedReadbackResult)
            {
                _loggedReadbackResult = true;
                LOG_ERROR("R32F staging CreateOffscreenPlainSurface failed: hr=0x{:08X}",
                          static_cast<uint32_t>(stagingHr));
            }
            return;
        }
    }

    const HRESULT readbackHr = _real->GetRenderTargetData(_depthCopyRTSurface, _depthStagingSurface);

    if (!_loggedReadbackResult)
    {
        _loggedReadbackResult = true;

        if (FAILED(readbackHr))
        {
            LOG_ERROR("Depth GetRenderTargetData (R32F) failed: hr=0x{:08X}",
                      static_cast<uint32_t>(readbackHr));
            return;
        }

        // Sanity-check: read the center pixel. INTZ depth is in [0,1] (post
        // perspective divide), so anything outside that range means the copy
        // path produced garbage.
        D3DLOCKED_RECT locked = {};
        const HRESULT lockHr = _depthStagingSurface->LockRect(&locked, nullptr, D3DLOCK_READONLY);
        if (SUCCEEDED(lockHr) && locked.pBits != nullptr)
        {
            const auto* row = static_cast<const float*>(locked.pBits) + (height / 2) * (locked.Pitch / sizeof(float));
            const float centerDepth = row[width / 2];
            _depthStagingSurface->UnlockRect();
            LOG_INFO("INTZ depth readback OK ({}x{}) — center pixel depth = {:.6f}",
                     width, height, centerDepth);
        }
        else
        {
            LOG_INFO("INTZ depth readback OK ({}x{}) — but LockRect failed: hr=0x{:08X}",
                     width, height, static_cast<uint32_t>(lockHr));
        }
    }
}

// =============================================================================
// IUnknown
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IDirect3DDevice9))
    {
        AddRef();
        *ppvObject = static_cast<IDirect3DDevice9*>(this);
        return S_OK;
    }
    else if (riid == __uuidof(IDirect3DDevice9Ex) && _realEx != nullptr)
    {
        AddRef();
        *ppvObject = static_cast<IDirect3DDevice9Ex*>(this);
        return S_OK;
    }
    else if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *ppvObject = static_cast<IUnknown*>(this);
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::AddRef()
{
    return InterlockedIncrement(&_refcount);
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Release()
{
    ULONG ref = InterlockedDecrement(&_refcount);
    if (ref == 0)
    {
        _real->Release();
        delete this;
    }
    return ref;
}

// =============================================================================
// IDirect3DDevice9 methods
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::TestCooperativeLevel()
{
    return _real->TestCooperativeLevel();
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetAvailableTextureMem()
{
    return _real->GetAvailableTextureMem();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EvictManagedResources()
{
    return _real->EvictManagedResources();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDirect3D(IDirect3D9** ppD3D9)
{
    return _real->GetDirect3D(ppD3D9);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDeviceCaps(D3DCAPS9* pCaps)
{
    return _real->GetDeviceCaps(pCaps);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode)
{
    return _real->GetDisplayMode(iSwapChain, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters)
{
    return _real->GetCreationParameters(pParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap)
{
    return _real->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCursorPosition(int X, int Y, DWORD Flags)
{
    _real->SetCursorPosition(X, Y, Flags);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ShowCursor(BOOL bShow)
{
    return _real->ShowCursor(bShow);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain)
{
    return _real->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain)
{
    return _real->GetSwapChain(iSwapChain, pSwapChain);
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetNumberOfSwapChains()
{
    return _real->GetNumberOfSwapChains();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    const bool needAutoDepth = _autoDepthSubstituted;
    InvalidateTrackedResources();

    if (pPresentationParameters)
        _presentParams = *pPresentationParameters;

    const HRESULT hr = _real->Reset(pPresentationParameters);

    // After a successful Reset we must re-establish our auto-depth INTZ — the
    // device dropped its default-pool surfaces (including ours) and the game
    // expects depth to "just work".
    if (SUCCEEDED(hr) && needAutoDepth)
    {
        _autoDepthSubstituted = true;
        InitAutoDepthIntz(_presentParams.BackBufferWidth, _presentParams.BackBufferHeight);
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    if (Config::Instance()->Dx9TAA.value_or_default())
    {
        if (!_loggedFirstFrameVSStats)
        {
            _loggedFirstFrameVSStats = true;
            LOG_INFO("Phase 7 probe: first frame VS constant calls={}, projection matches={}",
                     _vsConstCallsThisFrame, _projectionMatchCount);
        }
        _vsConstCallsThisFrame = 0;

        LogTopDepthStats();
        IdentifySceneDepth();
        AttemptDepthReadback();
    }

    return _real->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer)
{
    return _real->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus)
{
    return _real->GetRasterStatus(iSwapChain, pRasterStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetDialogBoxMode(BOOL bEnableDialogs)
{
    return _real->SetDialogBoxMode(bEnableDialogs);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp)
{
    _real->SetGammaRamp(iSwapChain, Flags, pRamp);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp)
{
    _real->GetGammaRamp(iSwapChain, pRamp);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
    return _real->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
{
    return _real->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
    return _real->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
    return _real->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
    return _real->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    return _real->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    // Phase 3 INTZ: if the game is creating a scene-sized non-MSAA D24-ish
    // depth surface, swap it for an INTZ texture's level-0 surface. INTZ is
    // bindable as both depth-stencil and sampleable texture, so we can read
    // it back later via a pixel-shader copy.
    if (Config::Instance()->Dx9TAA.value_or_default() &&
        _intzSupported &&
        Width == _presentParams.BackBufferWidth &&
        Height == _presentParams.BackBufferHeight &&
        MultiSample == D3DMULTISAMPLE_NONE &&
        (Format == D3DFMT_D24S8 || Format == D3DFMT_D24X8))
    {
        const D3DFORMAT intzFormat = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));

        IDirect3DTexture9* intzTex = nullptr;
        HRESULT hr = _real->CreateTexture(
            Width, Height, 1,
            D3DUSAGE_DEPTHSTENCIL,
            intzFormat,
            D3DPOOL_DEFAULT,
            &intzTex,
            nullptr);

        if (SUCCEEDED(hr) && intzTex != nullptr)
        {
            IDirect3DSurface9* intzSurf = nullptr;
            hr = intzTex->GetSurfaceLevel(0, &intzSurf);

            if (SUCCEEDED(hr) && intzSurf != nullptr)
            {
                // Record (surface, texture) so the readback path can look
                // up which INTZ-backed texture to sample for this surface.
                _intzTextureBySurface[intzSurf] = intzTex; // we own the texture ref

                // Force-update tracking to this INTZ surface. Same-size
                // replacement is skipped by the area heuristic in
                // SetDepthStencilSurface, so we update directly here.
                if (_trackedDepthSurface)
                    _trackedDepthSurface->Release();
                _trackedDepthSurface = intzSurf;
                _trackedDepthSurface->AddRef();
                _trackedDepthArea = Width * Height;
                _trackedDepthDesc.Width = Width;
                _trackedDepthDesc.Height = Height;
                _trackedDepthDesc.Format = intzFormat;
                _trackedDepthDesc.MultiSampleType = D3DMULTISAMPLE_NONE;

                // Re-arm logs so the next readback attempt reports its outcome.
                _loggedReadbackSkip = false;
                _loggedReadbackResult = false;

                *ppSurface = intzSurf; // game owns this ref

                if (!_loggedIntzCreation)
                {
                    _loggedIntzCreation = true;
                    LOG_INFO("INTZ depth surface created ({}x{}, in place of format=0x{:X})",
                             Width, Height, static_cast<uint32_t>(Format));
                }
                return S_OK;
            }

            intzTex->Release();
            LOG_WARN("INTZ GetSurfaceLevel failed: hr=0x{:08X}, falling back to normal depth",
                     static_cast<uint32_t>(hr));
        }
        else
        {
            LOG_WARN("INTZ CreateTexture failed (CheckDeviceFormat said OK): hr=0x{:08X}, falling back",
                     static_cast<uint32_t>(hr));
        }
    }

    return _real->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint)
{
    return _real->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture)
{
    return _real->UpdateTexture(pSourceTexture, pDestinationTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface)
{
    return _real->GetRenderTargetData(pRenderTarget, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface)
{
    return _real->GetFrontBufferData(iSwapChain, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
    return _real->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color)
{
    return _real->ColorFill(pSurface, pRect, color);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    return _real->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
    return _real->SetRenderTarget(RenderTargetIndex, pRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget)
{
    return _real->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil)
{
    // Phase 3 Mark 2: register/update the stats entry and mark it current so
    // subsequent Draw* calls accumulate against it.
    RegisterDepthSurfaceForStats(pNewZStencil);

    if (pNewZStencil)
    {
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(pNewZStencil->GetDesc(&desc)))
        {
            const UINT area = desc.Width * desc.Height;

            // Mark 1 area-based tracking — kept for now; Mark 2 scoring (commit 2)
            // will replace it as the source of _trackedDepthSurface.
            const bool matchesBackbuffer =
                _presentParams.BackBufferWidth > 0 &&
                desc.Width == _presentParams.BackBufferWidth &&
                desc.Height == _presentParams.BackBufferHeight;

            if (matchesBackbuffer && area > _trackedDepthArea)
            {
                if (_trackedDepthSurface)
                    _trackedDepthSurface->Release();

                _trackedDepthSurface = pNewZStencil;
                _trackedDepthSurface->AddRef();
                _trackedDepthArea = area;
                _trackedDepthDesc = desc;

                if (!_loggedDepthCapture)
                {
                    _loggedDepthCapture = true;
                    LOG_INFO("Tracked scene depth surface: {}x{}, format=0x{:X}, MS={}",
                             desc.Width, desc.Height,
                             static_cast<uint32_t>(desc.Format),
                             static_cast<int>(desc.MultiSampleType));
                }
            }
        }
    }

    return _real->SetDepthStencilSurface(pNewZStencil);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface)
{
    return _real->GetDepthStencilSurface(ppZStencilSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::BeginScene()
{
    // Tracking stub: increment frame index
    _frameIndex++;

    return _real->BeginScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EndScene()
{
    if (Config::Instance()->Dx9TAA.value_or_default())
    {
        // Flashing 16x16 square in the top-left corner: cycles R/G/B per frame
        // so the user can confirm the Dx9TAA path is live without reading the log.
        const D3DRECT indicator = { 8, 8, 24, 24 };
        const D3DCOLOR colors[3] = { D3DCOLOR_XRGB(255, 0, 0), D3DCOLOR_XRGB(0, 255, 0), D3DCOLOR_XRGB(0, 0, 255) };
        HRESULT hr = _real->Clear(1, &indicator, D3DCLEAR_TARGET, colors[_frameIndex % 3], 0.0f, 0);

        if (!_loggedIndicatorDraw)
        {
            _loggedIndicatorDraw = true;
            LOG_INFO("Dx9TAA: first indicator Clear, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    return _real->EndScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
    // Phase 3 Mark 2: clearing Z is the end-of-scene boundary for our stats.
    if (Flags & D3DCLEAR_ZBUFFER)
        ResetDepthStatsForCurrentZ();

    return _real->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    if (pMatrix)
    {
        switch (State)
        {
        case D3DTS_VIEW:
            _currentView = *pMatrix;
            break;
        case D3DTS_PROJECTION:
            _currentProjection = *pMatrix;

            if (Config::Instance()->Dx9TAA.value_or_default())
            {
                const UINT width = _presentParams.BackBufferWidth;
                const UINT height = _presentParams.BackBufferHeight;

                if (width > 0 && height > 0)
                {
                    // DLAA = scale 1.0 → 8 phases (AMD FSR2 formula ceil(8 * n²))
                    constexpr int32_t phaseCount = 8;
                    HaltonSequence::GetJitterOffset(_frameIndex, phaseCount, &_jitterX, &_jitterY);

                    // Y sign is AMD's DX12 reference; DX9 Y-convention may flip — verify visually.
                    const float clipX = 2.0f * _jitterX / static_cast<float>(width);
                    const float clipY = -2.0f * _jitterY / static_cast<float>(height);

                    D3DMATRIX jittered = *pMatrix;
                    jittered._31 += clipX;
                    jittered._32 += clipY;

                    if (!_loggedProjectionJitter)
                    {
                        _loggedProjectionJitter = true;
                        LOG_INFO("Dx9TAA: first projection jitter applied (px=[{:.3f},{:.3f}], clip=[{:.6f},{:.6f}], backbuf={}x{})",
                                 _jitterX, _jitterY, clipX, clipY, width, height);
                    }

                    return _real->SetTransform(State, &jittered);
                }
            }
            break;
        case D3DTS_WORLD:
            _currentWorld = *pMatrix;
            break;
        default:
            break;
        }
    }

    return _real->SetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)
{
    return _real->GetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    return _real->MultiplyTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetViewport(CONST D3DVIEWPORT9* pViewport)
{
    return _real->SetViewport(pViewport);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetViewport(D3DVIEWPORT9* pViewport)
{
    return _real->GetViewport(pViewport);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetMaterial(CONST D3DMATERIAL9* pMaterial)
{
    return _real->SetMaterial(pMaterial);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetMaterial(D3DMATERIAL9* pMaterial)
{
    return _real->GetMaterial(pMaterial);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetLight(DWORD Index, CONST D3DLIGHT9* pLight)
{
    return _real->SetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetLight(DWORD Index, D3DLIGHT9* pLight)
{
    return _real->GetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::LightEnable(DWORD Index, BOOL Enable)
{
    return _real->LightEnable(Index, Enable);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetLightEnable(DWORD Index, BOOL* pEnable)
{
    return _real->GetLightEnable(Index, pEnable);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetClipPlane(DWORD Index, CONST float* pPlane)
{
    return _real->SetClipPlane(Index, pPlane);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetClipPlane(DWORD Index, float* pPlane)
{
    return _real->GetClipPlane(Index, pPlane);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
    return _real->SetRenderState(State, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue)
{
    return _real->GetRenderState(State, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB)
{
    return _real->CreateStateBlock(Type, ppSB);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::BeginStateBlock()
{
    return _real->BeginStateBlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::EndStateBlock(IDirect3DStateBlock9** ppSB)
{
    return _real->EndStateBlock(ppSB);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus)
{
    return _real->SetClipStatus(pClipStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetClipStatus(D3DCLIPSTATUS9* pClipStatus)
{
    return _real->GetClipStatus(pClipStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture)
{
    return _real->GetTexture(Stage, ppTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture)
{
    return _real->SetTexture(Stage, pTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue)
{
    return _real->GetTextureStageState(Stage, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    return _real->SetTextureStageState(Stage, Type, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue)
{
    return _real->GetSamplerState(Sampler, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
    return _real->SetSamplerState(Sampler, Type, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ValidateDevice(DWORD* pNumPasses)
{
    return _real->ValidateDevice(pNumPasses);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries)
{
    return _real->SetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries)
{
    return _real->GetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetCurrentTexturePalette(UINT PaletteNumber)
{
    return _real->SetCurrentTexturePalette(PaletteNumber);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetCurrentTexturePalette(UINT* PaletteNumber)
{
    return _real->GetCurrentTexturePalette(PaletteNumber);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetScissorRect(CONST RECT* pRect)
{
    return _real->SetScissorRect(pRect);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetScissorRect(RECT* pRect)
{
    return _real->GetScissorRect(pRect);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetSoftwareVertexProcessing(BOOL bSoftware)
{
    return _real->SetSoftwareVertexProcessing(bSoftware);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetSoftwareVertexProcessing()
{
    return _real->GetSoftwareVertexProcessing();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetNPatchMode(float nSegments)
{
    return _real->SetNPatchMode(nSegments);
}

float STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetNPatchMode()
{
    return _real->GetNPatchMode();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    AccumulateDrawStats(PrimitiveType, PrimitiveCount, 0);
    return _real->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
    AccumulateDrawStats(PrimitiveType, primCount, NumVertices);
    return _real->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    AccumulateDrawStats(PrimitiveType, PrimitiveCount, 0);
    return _real->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    AccumulateDrawStats(PrimitiveType, PrimitiveCount, NumVertices);
    return _real->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDecl, DWORD Flags)
{
    return _real->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl)
{
    return _real->CreateVertexDeclaration(pVertexElements, ppDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl)
{
    return _real->SetVertexDeclaration(pDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl)
{
    return _real->GetVertexDeclaration(ppDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetFVF(DWORD FVF)
{
    return _real->SetFVF(FVF);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetFVF(DWORD* pFVF)
{
    return _real->GetFVF(pFVF);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader)
{
    return _real->CreateVertexShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShader(IDirect3DVertexShader9* pShader)
{
    return _real->SetVertexShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShader(IDirect3DVertexShader9** ppShader)
{
    return _real->GetVertexShader(ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
    if (Config::Instance()->Dx9TAA.value_or_default() && pConstantData)
    {
        _vsConstCallsThisFrame++;
        ProbeForProjectionMatrix(StartRegister, pConstantData, Vector4fCount);
    }

    return _real->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
    return _real->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
    return _real->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
    return _real->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
    return _real->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
    return _real->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride)
{
    return _real->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride)
{
    return _real->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetStreamSourceFreq(UINT StreamNumber, UINT Setting)
{
    return _real->SetStreamSourceFreq(StreamNumber, Setting);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting)
{
    return _real->GetStreamSourceFreq(StreamNumber, pSetting);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetIndices(IDirect3DIndexBuffer9* pIndexData)
{
    return _real->SetIndices(pIndexData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetIndices(IDirect3DIndexBuffer9** ppIndexData)
{
    return _real->GetIndices(ppIndexData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader)
{
    return _real->CreatePixelShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShader(IDirect3DPixelShader9* pShader)
{
    return _real->SetPixelShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShader(IDirect3DPixelShader9** ppShader)
{
    return _real->GetPixelShader(ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount)
{
    return _real->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount)
{
    return _real->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount)
{
    return _real->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount)
{
    return _real->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT BoolCount)
{
    return _real->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount)
{
    return _real->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo)
{
    return _real->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo)
{
    return _real->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::DeletePatch(UINT Handle)
{
    return _real->DeletePatch(Handle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery)
{
    return _real->CreateQuery(Type, ppQuery);
}

// =============================================================================
// IDirect3DDevice9Ex methods
// =============================================================================

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetConvolutionMonoKernel(UINT width, UINT height, float* rows, float* columns)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetConvolutionMonoKernel(width, height, rows, columns);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ComposeRects(IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst, IDirect3DVertexBuffer9* pSrcRectDescs, UINT NumRects, IDirect3DVertexBuffer9* pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset, int Yoffset)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->ComposeRects(pSrc, pDst, pSrcRectDescs, NumRects, pDstRectDescs, Operation, Xoffset, Yoffset);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::PresentEx(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    if (Config::Instance()->Dx9TAA.value_or_default())
    {
        if (!_loggedFirstFrameVSStats)
        {
            _loggedFirstFrameVSStats = true;
            LOG_INFO("Phase 7 probe: first frame VS constant calls={}, projection matches={}",
                     _vsConstCallsThisFrame, _projectionMatchCount);
        }
        _vsConstCallsThisFrame = 0;

        LogTopDepthStats();
        IdentifySceneDepth();
        AttemptDepthReadback();
    }

    return _realEx->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetGPUThreadPriority(INT* pPriority)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetGPUThreadPriority(pPriority);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetGPUThreadPriority(INT Priority)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetGPUThreadPriority(Priority);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::WaitForVBlank(UINT iSwapChain)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->WaitForVBlank(iSwapChain);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CheckResourceResidency(IDirect3DResource9** pResourceArray, UINT32 NumResources)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CheckResourceResidency(pResourceArray, NumResources);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::SetMaximumFrameLatency(UINT MaxLatency)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetMaximumFrameLatency(pMaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CheckDeviceState(HWND hDestinationWindow)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CheckDeviceState(hDestinationWindow);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CreateRenderTargetEx(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->CreateOffscreenPlainSurfaceEx(Width, Height, Format, Pool, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle, DWORD Usage)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    // Same INTZ substitution as the non-Ex path. Usage flag is forwarded as-is
    // to CreateTexture (which accepts D3DUSAGE_DEPTHSTENCIL plus the optional
    // shared flag — INTZ + shared is driver-dependent but matches what the
    // game asked for).
    if (Config::Instance()->Dx9TAA.value_or_default() &&
        _intzSupported &&
        Width == _presentParams.BackBufferWidth &&
        Height == _presentParams.BackBufferHeight &&
        MultiSample == D3DMULTISAMPLE_NONE &&
        (Format == D3DFMT_D24S8 || Format == D3DFMT_D24X8))
    {
        const D3DFORMAT intzFormat = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));

        IDirect3DTexture9* intzTex = nullptr;
        HRESULT hr = _real->CreateTexture(
            Width, Height, 1,
            D3DUSAGE_DEPTHSTENCIL | Usage,
            intzFormat,
            D3DPOOL_DEFAULT,
            &intzTex,
            nullptr);

        if (SUCCEEDED(hr) && intzTex != nullptr)
        {
            IDirect3DSurface9* intzSurf = nullptr;
            hr = intzTex->GetSurfaceLevel(0, &intzSurf);

            if (SUCCEEDED(hr) && intzSurf != nullptr)
            {
                _intzTextureBySurface[intzSurf] = intzTex; // we own the texture ref

                if (_trackedDepthSurface)
                    _trackedDepthSurface->Release();
                _trackedDepthSurface = intzSurf;
                _trackedDepthSurface->AddRef();
                _trackedDepthArea = Width * Height;
                _trackedDepthDesc.Width = Width;
                _trackedDepthDesc.Height = Height;
                _trackedDepthDesc.Format = intzFormat;
                _trackedDepthDesc.MultiSampleType = D3DMULTISAMPLE_NONE;

                _loggedReadbackSkip = false;
                _loggedReadbackResult = false;

                *ppSurface = intzSurf;

                if (!_loggedIntzCreation)
                {
                    _loggedIntzCreation = true;
                    LOG_INFO("INTZ depth surface created via Ex ({}x{}, in place of format=0x{:X})",
                             Width, Height, static_cast<uint32_t>(Format));
                }
                return S_OK;
            }

            intzTex->Release();
        }
    }

    return _realEx->CreateDepthStencilSurfaceEx(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle, Usage);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::ResetEx(D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    const bool needAutoDepth = _autoDepthSubstituted;
    InvalidateTrackedResources();

    if (pPresentationParameters)
        _presentParams = *pPresentationParameters;

    const HRESULT hr = _realEx->ResetEx(pPresentationParameters, pFullscreenDisplayMode);

    if (SUCCEEDED(hr) && needAutoDepth)
    {
        _autoDepthSubstituted = true;
        InitAutoDepthIntz(_presentParams.BackBufferWidth, _presentParams.BackBufferHeight);
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9Ex::GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation)
{
    if (_realEx == nullptr)
        return E_NOTIMPL;

    return _realEx->GetDisplayModeEx(iSwapChain, pMode, pRotation);
}
