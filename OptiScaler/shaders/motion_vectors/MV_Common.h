#pragma once
#include "SysUtils.h"
#include <d3dcompiler.h>

struct alignas(16) MVConstants
{
    float InvViewProj_Current[16];
    float ViewProj_Previous[16];
    float ScreenWidth;
    float ScreenHeight;
    float Padding[2];
};
static_assert(sizeof(MVConstants) % 16 == 0, "cbuffer must be 16-byte aligned");

// Source kept in sync with shaders/motion_vectors/precompile/mv.hlsl.
// The precompile pipeline (fxc.exe → header) is a Windows-only step done
// out of band; the runtime fallback compiles this string via D3DCompile.
inline static std::string mvShaderCode = R"(
cbuffer MVConstants : register(b0)
{
    row_major float4x4 InvViewProj_Current;
    row_major float4x4 ViewProj_Previous;
    float2 ScreenDimensions;
    float2 Padding;
};

Texture2D<float>    SourceDepth   : register(t0);
RWTexture2D<float2> DestinationMV : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadID.xy;
    if (pixelPos.x >= (uint) ScreenDimensions.x || pixelPos.y >= (uint) ScreenDimensions.y)
        return;

    float depth = SourceDepth.Load(int3(pixelPos, 0));

    float2 uvCur = (float2(pixelPos) + 0.5f) / ScreenDimensions;
    float2 ndcXY = float2(uvCur.x * 2.0f - 1.0f, 1.0f - uvCur.y * 2.0f);

    float4 clipPosCur = float4(ndcXY, depth, 1.0f);
    float4 worldPos   = mul(clipPosCur, InvViewProj_Current);
    worldPos /= worldPos.w;

    float4 clipPosPrev = mul(worldPos, ViewProj_Previous);
    float2 ndcPrev = clipPosPrev.xy / clipPosPrev.w;
    float2 uvPrev  = float2(ndcPrev.x * 0.5f + 0.5f, 0.5f - ndcPrev.y * 0.5f);

    DestinationMV[pixelPos] = uvPrev - uvCur;
})";

// Dynamic D3DCompile to keep Win32 link-deps minimal — matches Phase 3 (see
// EnsureDepthCopyPS). Caller releases the returned blob.
inline static ID3DBlob* MV_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
{
    HMODULE d3dcompilerDll = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dcompilerDll)
    {
        LOG_ERROR("MV: d3dcompiler_47.dll not loadable");
        return nullptr;
    }

    using D3DCompile_pfn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*,
                                            LPCSTR, LPCSTR, UINT, UINT,
                                            ID3DBlob**, ID3DBlob**);
    auto D3DCompile_fn = reinterpret_cast<D3DCompile_pfn>(GetProcAddress(d3dcompilerDll, "D3DCompile"));
    if (!D3DCompile_fn)
    {
        FreeLibrary(d3dcompilerDll);
        LOG_ERROR("MV: D3DCompile entry point missing");
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
            LOG_ERROR("MV shader compile error: {0}", (char*) errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        else
        {
            LOG_ERROR("MV shader compile error: {0:x}", (UINT) hr);
        }
        if (shaderBlob)
            shaderBlob->Release();
        return nullptr;
    }

    if (errorBlob)
        errorBlob->Release();
    return shaderBlob;
}
