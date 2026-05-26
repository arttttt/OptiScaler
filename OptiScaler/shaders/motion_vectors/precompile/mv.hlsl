cbuffer MVConstants : register(b0)
{
    row_major float4x4 InvViewProj_Current;
    row_major float4x4 ViewProj_Previous;
    float2 ScreenDimensions;
    float2 Padding;
};

Texture2D<float>    SourceDepth   : register(t0);
RWTexture2D<float2> DestinationMV : register(u0);

// Camera-only motion vectors from depth.
// MV[uv] = uv_prev - uv_cur  (FSR2 convention: where this pixel came from).
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadID.xy;
    if (pixelPos.x >= (uint) ScreenDimensions.x || pixelPos.y >= (uint) ScreenDimensions.y)
        return;

    float depth = SourceDepth.Load(int3(pixelPos, 0));

    float2 uvCur = (float2(pixelPos) + 0.5f) / ScreenDimensions;
    // D3D NDC: Y up, UV Y down.
    float2 ndcXY = float2(uvCur.x * 2.0f - 1.0f, 1.0f - uvCur.y * 2.0f);

    float4 clipPosCur = float4(ndcXY, depth, 1.0f);
    float4 worldPos   = mul(clipPosCur, InvViewProj_Current);
    worldPos /= worldPos.w;

    float4 clipPosPrev = mul(worldPos, ViewProj_Previous);
    float2 ndcPrev = clipPosPrev.xy / clipPosPrev.w;
    float2 uvPrev  = float2(ndcPrev.x * 0.5f + 0.5f, 0.5f - ndcPrev.y * 0.5f);

    DestinationMV[pixelPos] = uvPrev - uvCur;
}
