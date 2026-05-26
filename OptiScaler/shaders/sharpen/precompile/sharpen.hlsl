cbuffer SharpenConstants : register(b0)
{
    float Sharpness;
    int   DebugMode;       // 0 = sharpen, 1 = invert (unmissable proof-of-path)
    float Padding[2];
};

Texture2D<float4>    Source : register(t0);
RWTexture2D<float4>  Dest   : register(u0);

// Bridge dispatch CS. Two modes:
//   DebugMode == 1 → invert colors. Picked by default until the user
//     confirms the bridge is actually running on their game. Impossible
//     to mistake for a passthrough.
//   DebugMode == 0 → 4-tap unsharp mask, controlled by Sharpness. Subtle
//     by design — turn on after the inversion test passes.
// Both replaced by FSR2 Evaluate once an x86 FFX bundle exists.
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
}
