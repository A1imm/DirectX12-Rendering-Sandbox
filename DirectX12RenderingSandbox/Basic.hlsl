cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;

    float3 gCameraPosition;
    float gPadding;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;

    float3 WorldPosition : POSITION0;
    float3 WorldNormal : NORMAL;

    float2 UV : TEXCOORD;
};

VertexOutput VS_Main(VertexInput input)
{
    VertexOutput output;

    float4 worldPosition =
        mul(
            float4(input.Position, 1.0f),
            gWorld
        );

    output.WorldPosition =
        worldPosition.xyz;

    output.Position =
        mul(
            float4(input.Position, 1.0f),
            gWorldViewProj
        );

    // Correct for the current uniform object scaling.
    output.WorldNormal =
        normalize(
            mul(
                input.Normal,
                (float3x3) gWorld
            )
        );

    output.UV = input.UV;

    return output;
}

float4 PS_Main(VertexOutput input) : SV_Target
{
    float4 albedo =
        gTexture.Sample(
            gSampler,
            input.UV
        );

    float3 normal =
        normalize(input.WorldNormal);

    // Direction in which the light rays travel.
    float3 lightDirection =
        normalize(
            float3(
                0.5f,
                -1.0f,
                0.4f
            )
        );

    float3 toLight =
        -lightDirection;

    // Ambient
    float ambientStrength = 0.20f;

    // Lambert diffuse
    float diffuseStrength =
        saturate(
            dot(
                normal,
                toLight
            )
        );

    // Blinn-Phong specular
    float3 viewDirection =
        normalize(
            gCameraPosition -
            input.WorldPosition
        );

    float3 halfVector =
        normalize(
            toLight +
            viewDirection
        );

    float specularStrength =
        pow(
            saturate(
                dot(
                    normal,
                    halfVector
                )
            ),
            32.0f
        );

    float3 lighting =
        ambientStrength
        +
        0.80f * diffuseStrength;

    float3 finalColor =
        albedo.rgb * lighting
        +
        0.25f * specularStrength;

    return float4(
        finalColor,
        albedo.a
    );
}