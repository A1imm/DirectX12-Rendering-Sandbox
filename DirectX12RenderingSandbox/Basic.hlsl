cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float3 gCameraPosition;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VertexInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
};

VertexOutput VS_Main(VertexInput input)
{
    VertexOutput output;

    output.Position =
        mul(float4(input.Position, 1.0f), gWorldViewProj);

    output.UV = input.UV;

    return output;
}

float4 PS_Main(VertexOutput input) : SV_Target
{
    return gTexture.Sample(gSampler, input.UV);
}