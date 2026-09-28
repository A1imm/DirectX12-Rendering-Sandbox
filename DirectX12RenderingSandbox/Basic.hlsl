cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;

    float3 gCameraPosition;
    float gPadding;
};

cbuffer cbScene : register(b1)
{
    float3 gLightDirection;
    float gLightIntensity;

    float3 gLightColor;
    float gAmbientStrength;

    float gSpecularStrength;
    float gShininess;

    float2 gScenePadding;
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

    float3 lightDirection =
    normalize(gLightDirection);

    float3 toLight =
    -lightDirection;

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
        gShininess
    );

    float3 lighting =
        gAmbientStrength
        +
        gLightColor *
        gLightIntensity *
        diffuseStrength;

    float3 finalColor =
        albedo.rgb * lighting
        +
        gLightColor *
        gSpecularStrength *
        specularStrength;

    return float4(
        finalColor,
        albedo.a
    );
}