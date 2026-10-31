#include "Common.hlsli"

Texture2D Scene : register(t0);
Texture2D History : register(t1);
Texture2D Bloom0 : register(t2);
Texture2D Bloom1 : register(t3);
Texture2D Bloom2 : register(t4);
Texture2D Bloom3 : register(t5);
Texture2D Bloom4 : register(t6);
SamplerState LinearClamp : register(s0);

float4 AccumulatePS(VertexOutput input) : SV_TARGET
{
    float3 current = Scene.SampleLevel(LinearClamp, input.uv, 0).rgb;
    float3 previous = History.SampleLevel(LinearClamp, input.uv, 0).rgb;
    return float4(lerp(previous, current, Sampling.z), 1);
}

float4 DownsamplePS(VertexOutput input) : SV_TARGET
{
    float3 sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        float3 c = Scene.SampleLevel(LinearClamp, input.uv + float2(x,y) * FilterParams.xy * Post.z, 0).rgb;
        if (FilterParams.z > 0.5)
        {
            float l = max(c.r, max(c.g, c.b));
            float soft = clamp(l - Post.y + 0.5, 0, 1);
            soft = soft * soft * 0.5;
            c *= max(l - Post.y, soft) / max(l, 0.00001);
        }
        float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
        sum += c * weight;
    }
    return float4(sum / 16.0, 1);
}

float3 ToneMap(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 CompositePS(VertexOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float3 c = Scene.SampleLevel(LinearClamp, uv, 0).rgb;
    float3 bloom = Bloom0.SampleLevel(LinearClamp, uv, 0).rgb * 0.16
                 + Bloom1.SampleLevel(LinearClamp, uv, 0).rgb * 0.20
                 + Bloom2.SampleLevel(LinearClamp, uv, 0).rgb * 0.22
                 + Bloom3.SampleLevel(LinearClamp, uv, 0).rgb * 0.22
                 + Bloom4.SampleLevel(LinearClamp, uv, 0).rgb * 0.20;
    float3 streak = 0;
    [unroll] for (int i = -8; i <= 8; ++i)
        streak += Bloom2.SampleLevel(LinearClamp, uv + float2(i * 0.014, 0), 0).rgb * exp(-abs(i) * 0.35);
    c += Post.x * bloom + Post.w * streak * 0.16;
    c *= 1.0 - Background.z * pow(saturate(length((uv - 0.5) * 1.25)), 2.0);
    c = ToneMap(c * UpExposure.w);
    c = pow(c, 1.0 / 2.2);
    float dither = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715)))) - 0.5;
    return float4(saturate(c + dither / 255.0), 1);
}
