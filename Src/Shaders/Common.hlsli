cbuffer FrameConstants : register(b0)
{
    float4 EyeTime;
    float4 ForwardFov;
    float4 RightAspect;
    float4 UpExposure;
    float4 Resolution;
    float4 DiskGeometry;
    float4 DiskAppearance;
    float4 Relativity;
    float4 Background;
    float4 Post;
    float4 Integration;
    float4 Sampling;
    float4 DiskNormal;
    float4 FilterParams;
};

static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput FullscreenVS(uint id : SV_VertexID)
{
    VertexOutput o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
