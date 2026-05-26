cbuffer SharpenConstants : register(b0)
{
    float Sharpness;
    float Padding[3];
};

Texture2D<float4>    Source : register(t0);
RWTexture2D<float4>  Dest   : register(u0);

// 4-tap unsharp mask. Cheap, no temporal state — purpose is to make it
// visible that the bridge is doing real DX11 work and not just blitting.
// Replaced by FSR2 dispatch in 5b-followup when the x86 FFX bundle lands.
[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    int3 cp = int3(dtid.xy, 0);

    uint w, h;
    Dest.GetDimensions(w, h);
    if (dtid.x >= w || dtid.y >= h)
        return;

    float3 c = Source.Load(cp).rgb;
    float3 n = Source.Load(cp + int3( 0,  1, 0)).rgb;
    float3 s = Source.Load(cp + int3( 0, -1, 0)).rgb;
    float3 e = Source.Load(cp + int3( 1,  0, 0)).rgb;
    float3 w4 = Source.Load(cp + int3(-1,  0, 0)).rgb;

    float3 avg   = (n + s + e + w4) * 0.25;
    float3 sharp = c + (c - avg) * Sharpness;

    Dest[dtid.xy] = float4(saturate(sharp), 1.0);
}
