#pragma once
#include "SysUtils.h"
#include <d3dcompiler.h>

struct alignas(16) SharpenConstants
{
    float Sharpness;
    int   DebugMode;         // 0 = sharpen, 1 = invert
    float Padding[2];
};
static_assert(sizeof(SharpenConstants) % 16 == 0, "cbuffer must be 16-byte aligned");

// Keep in sync with shaders/sharpen/precompile/sharpen.hlsl. Runtime compile
// via dynamically-loaded d3dcompiler_47 matches Phase 3 / Phase 4 pattern,
// avoiding link-time dependency for the Win32 build.
inline static std::string sharpenShaderCode = R"(
cbuffer SharpenConstants : register(b0)
{
    float Sharpness;
    int   DebugMode;
    float Padding[2];
};

Texture2D<float4>   Source : register(t0);
RWTexture2D<float4> Dest   : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    int3 cp = int3(dtid.xy, 0);

    uint w, h;
    Dest.GetDimensions(w, h);
    if (dtid.x >= w || dtid.y >= h)
        return;

    float3 c = Source.Load(cp).rgb;

    if (DebugMode == 1)
    {
        Dest[dtid.xy] = float4(1.0 - c, 1.0);
        return;
    }

    float3 n  = Source.Load(cp + int3( 0,  1, 0)).rgb;
    float3 s  = Source.Load(cp + int3( 0, -1, 0)).rgb;
    float3 e  = Source.Load(cp + int3( 1,  0, 0)).rgb;
    float3 w4 = Source.Load(cp + int3(-1,  0, 0)).rgb;

    float3 avg   = (n + s + e + w4) * 0.25;
    float3 sharp = c + (c - avg) * Sharpness;

    Dest[dtid.xy] = float4(saturate(sharp), 1.0);
})";

inline static ID3DBlob* Sharpen_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
{
    HMODULE d3dcompilerDll = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dcompilerDll)
    {
        LOG_ERROR("Sharpen: d3dcompiler_47.dll not loadable");
        return nullptr;
    }

    using D3DCompile_pfn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*,
                                            LPCSTR, LPCSTR, UINT, UINT,
                                            ID3DBlob**, ID3DBlob**);
    auto D3DCompile_fn = reinterpret_cast<D3DCompile_pfn>(GetProcAddress(d3dcompilerDll, "D3DCompile"));
    if (!D3DCompile_fn)
    {
        FreeLibrary(d3dcompilerDll);
        LOG_ERROR("Sharpen: D3DCompile entry point missing");
        return nullptr;
    }

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile_fn(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr,
                               entryPoint, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                               &shaderBlob, &errorBlob);

    FreeLibrary(d3dcompilerDll);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            LOG_ERROR("Sharpen shader compile error: {0}", (char*) errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        else
        {
            LOG_ERROR("Sharpen shader compile error: {0:x}", (UINT) hr);
        }
        if (shaderBlob)
            shaderBlob->Release();
        return nullptr;
    }

    if (errorBlob)
        errorBlob->Release();
    return shaderBlob;
}
