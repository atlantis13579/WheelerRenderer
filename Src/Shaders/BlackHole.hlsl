#include "Common.hlsli"
#include "../Physics/GeodesicShared.h"

float Hash(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float Noise(float3 p)
{
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(Hash(i), Hash(i + float3(1,0,0)), f.x),
                     lerp(Hash(i + float3(0,1,0)), Hash(i + float3(1,1,0)), f.x), f.y),
                lerp(lerp(Hash(i + float3(0,0,1)), Hash(i + float3(1,0,1)), f.x),
                     lerp(Hash(i + float3(0,1,1)), Hash(i + float3(1,1,1)), f.x), f.y), f.z);
}

float Fbm(float3 p)
{
    float value = 0.5 * Noise(p);
    value += 0.25 * Noise(p * 2.03 + 4.7);
    value += 0.125 * Noise(p * 4.11 + 9.1);
    return value;
}

// A compact RGB approximation to blackbody chromaticity, normalized near white.
float3 Blackbody(float kelvin)
{
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float3 c;
    c.r = t <= 66.0 ? 1.0 : 1.292936 * pow(t - 60.0, -0.133205);
    c.g = t <= 66.0 ? 0.390082 * log(t) - 0.631841 : 1.129891 * pow(t - 60.0, -0.075515);
    c.b = t >= 66.0 ? 1.0 : t <= 19.0 ? 0.0 : 0.543207 * log(t - 10.0) - 1.196254;
    return pow(saturate(c), 2.2);
}

float3 DiskEmission(float3 p, float radius, float lambda, float observerLapse)
{
    float3 n = DiskNormal.xyz;
    float3 diskX = float3(1, 0, 0);
    float3 diskZ = cross(diskX, n);
    float angle = atan2(dot(p, diskZ), dot(p, diskX));
    float omega = sqrt(0.5 / (radius * radius * radius));
    float phase = angle - EyeTime.w * omega * 7.0 * DiskAppearance.w;
    float spiral = phase + 3.2 * log(radius);
    float3 samplePoint = float3(cos(phase) * radius, sin(phase) * radius, radius * 0.9);
    float clouds = Fbm(samplePoint * float3(1.8, 1.8, 3.0));
    float fine = Noise(samplePoint * float3(9.0, 9.0, 5.0));
    float filaments = sin(radius * 32.0 + 2.0 * sin(spiral * 5.0) + clouds * 9.0);
    float bands = 0.72 + 0.28 * sin(radius * 11.5 + 1.6 * sin(spiral * 3.0));
    float structure = lerp(1.0, (0.18 + 2.4 * clouds * clouds) * (0.55 + 0.45 * fine)
                           * (0.64 + 0.36 * filaments) * bands, DiskAppearance.z);
    float q = DiskGeometry.x / radius;
    float flux = pow(q, 3.0) * max(1.0 - sqrt(q), 0.0);
    float temperatureRatio = pow(max(flux / 0.05665, 0.00001), 0.25);
    float edge = smoothstep(DiskGeometry.x, DiskGeometry.x + 0.15, radius)
               * (1.0 - smoothstep(DiskGeometry.y * 0.8, DiskGeometry.y, radius));
    float grav = sqrt(max(1.0 - 1.0 / radius, 0.01)) / observerLapse;
    float beta2 = 0.5 / (radius - 1.0);
    float doppler = sqrt(1.0 - beta2) / max(1.0 - omega * lambda, 0.15);
    float shift = pow(grav, Relativity.y) * pow(doppler, Relativity.x);
    float3 color = Blackbody(DiskGeometry.z * temperatureRatio * shift);
    return color * DiskGeometry.w * pow(temperatureRatio, 2.5) * pow(shift, 4.0) * structure * edge;
}

float3 Stars(float3 direction)
{
    float2 uv = float2(atan2(direction.z, direction.x) / TWO_PI + 0.5, acos(clamp(direction.y, -1.0, 1.0)) / PI);
    // Derivatives are evaluated after all geodesics converge; the footprint follows lens magnification.
    float2 dx = ddx(uv), dy = ddy(uv);
    dx.x -= round(dx.x); dy.x -= round(dy.x);
    float footprint = clamp(max(length(dx), length(dy)), 0.00008, 0.008);
    float3 stars = 0;
    [unroll] for (int layer = 0; layer < 2; ++layer)
    {
        float2 grid = float2(700, 350) * (layer == 0 ? 1.0 : 0.47);
        float2 cell = floor(uv * grid);
        float2 f = frac(uv * grid);
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y);
            float2 key = cell + offset;
            key.x = key.x - floor(key.x / grid.x) * grid.x;
            float seed = Hash(float3(key, layer + 1));
            float2 center = float2(Hash(float3(key, 7)), Hash(float3(key, 19)));
            float radius = layer == 0 ? 0.045 : 0.026;
            float filterWidth = max(footprint * grid.y * 0.6, radius);
            float d = length(f - offset - center);
            float star = exp(-d * d / (filterWidth * filterWidth)) * (radius * radius / (filterWidth * filterWidth));
            float brightness = seed > 0.96 ? 24.0 : 1.7;
            stars += star * step(0.90, seed) * brightness * lerp(float3(0.52, 0.68, 1.0), float3(1.0, 0.77, 0.48), center.x);
        }
    }
    float latitude = dot(direction, normalize(float3(0.35, 0.85, 0.38)));
    float band = exp(-latitude * latitude * 42.0);
    float dust = Fbm(direction * 9.0 + 17.0);
    float haze = band * (0.05 + dust * dust * 0.35);
    float3 galaxy = lerp(float3(0.14, 0.19, 0.30), float3(0.42, 0.31, 0.20), dust) * haze;
    return stars * Background.x + (galaxy + float3(0.002, 0.003, 0.006)) * Background.y;
}

struct RayResult
{
    float3 radiance;
    float3 sky;
    float transmission;
    float winding;
    float iterations;
    float captured;
    float exhausted;
};

RayResult Trace(float3 ray)
{
    RayResult result = (RayResult)0;
    result.sky = ray;
    result.transmission = 1.0;
    float radius = length(EyeTime.xyz);
    float3 er = EyeTime.xyz / radius;
    float mu = clamp(dot(er, ray), -1.0, 1.0);
    float sine = length(cross(er, ray));
    float3 et = sine > 1e-7 ? normalize(ray - mu * er) : normalize(cross(er, abs(er.y) < 0.9 ? float3(0,1,0) : float3(1,0,0)));
    float u0 = 1.0 / radius;
    float lapse = sqrt(max(1.0 - u0, 0.00001));

    if (Relativity.w < 0.5)
    {
        float b2 = radius * radius * (1.0 - mu * mu);
        float horizonT = mu < 0.0 && b2 < 1.0 ? -radius * mu - sqrt(1.0 - b2) : 1e8;
        float denom = dot(ray, DiskNormal.xyz);
        float t = -dot(EyeTime.xyz, DiskNormal.xyz) / (abs(denom) > 1e-7 ? denom : 1e-7);
        float3 p = EyeTime.xyz + ray * t;
        float r = length(p);
        if (Relativity.z > 0.5 && t > 0 && t < horizonT && r > DiskGeometry.x && r < DiskGeometry.y)
        {
            result.radiance = DiskEmission(p, r, 0, 1);
            result.transmission = 1.0 - DiskAppearance.x;
        }
        if (horizonT < 1e7) { result.transmission = 0; result.captured = 1; }
        return result;
    }

    // Pure radial rays have no orbital plane and never reach a nonradial disk.
    if (sine < 1e-6)
    {
        result.captured = mu < 0 ? 1 : 0;
        result.transmission = 1.0 - result.captured;
        return result;
    }

    float b = radius * sine / lapse;
    // Backward tracing reverses the future-directed photon's angular momentum.
    float lambda = -b * dot(cross(er, et), DiskNormal.xyz);
    OrbitState state;
    state.u = u0;
    state.v = -u0 * mu * lapse / sine;
    float A = dot(er, DiskNormal.xyz);
    float B = dot(et, DiskNormal.xyz);
    float nextDisk = atan2(-A, B);
    if (nextDisk <= 0.00001) nextDisk += PI;
    float phi = 0;

    [loop] for (int iteration = 0; iteration < (int)Integration.y; ++iteration)
    {
        // Limit radial changes for near-radial rays; land exactly on disk crossings.
        float h = min(Integration.x, 0.08 / max(abs(state.v), 0.001));
        h = min(h, nextDisk - phi);
        OrbitState previous = state;
        state = StepOrbit(state, h);
        phi += h;
        result.iterations = iteration + 1;
        if (state.u >= 1.0)
        {
            result.transmission = 0;
            result.captured = 1;
            break;
        }
        if (state.u <= 0.0)
        {
            // Interpolate the u=0 asymptote; er(phi) is its direction at infinity.
            float escapePhi = phi - h + h * previous.u / max(previous.u - state.u, 1e-12);
            result.sky = er * cos(escapePhi) + et * sin(escapePhi);
            break;
        }
        if (phi >= nextDisk - 0.000001)
        {
            float r = 1.0 / state.u;
            if (Relativity.z > 0.5 && r > DiskGeometry.x && r < DiskGeometry.y)
            {
                float3 p = (er * cos(phi) + et * sin(phi)) * r;
                float opacity = DiskAppearance.x;
                result.radiance += result.transmission * opacity * DiskEmission(p, r, lambda, lapse);
                result.transmission *= 1.0 - opacity;
                // Keep tracing for diagnostics even after optically thick emission.
                if (result.transmission < 0.002 && Integration.z < 0.5) break;
            }
            nextDisk += PI;
        }
        if (iteration == (int)Integration.y - 1)
        {
            result.transmission = 0;
            result.exhausted = 1;
        }
    }
    result.winding = phi / TWO_PI;
    return result;
}

float4 BlackHolePS(VertexOutput input) : SV_TARGET
{
    float2 uv = input.uv + Sampling.xy * Resolution.zw;
    float2 screen = (uv * 2.0 - 1.0) * float2(RightAspect.w, -1.0);
    float3 ray = normalize(ForwardFov.xyz + (screen.x * RightAspect.xyz + screen.y * UpExposure.xyz) * ForwardFov.w);
    RayResult result = Trace(ray);
    float3 sky = Stars(normalize(result.sky));
    float3 color = result.radiance + result.transmission * sky;
    if (Integration.z > 0.5 && Integration.z < 1.5)
        color = lerp(float3(0.02, 0.09, 0.16), float3(0.9, 0.29, 0.035), saturate(result.winding));
    else if (Integration.z > 1.5 && Integration.z < 2.5)
        color = result.exhausted > 0.5 ? float3(1,0,1) : lerp(float3(0.06,0.16,0.23), float3(1,0.55,0.16), result.iterations / Integration.y);
    else if (Integration.z > 2.5)
        color = result.exhausted > 0.5 ? float3(1,0,1) : result.captured > 0.5 ? 0 : float3(0.6, 0.75, 0.9);
    return float4(min(max(color, 0), 60000.0), 1);
}
