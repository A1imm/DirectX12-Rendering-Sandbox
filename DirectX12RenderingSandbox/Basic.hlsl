#include "ShaderConstants.hlsli"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
    float4x4 gWorldLightViewProj;
    
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
Texture2D gNormalMap : register(t3);
Texture2D<float> gShadowMap : register(t4);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

#include "ShadowCommon.hlsli"

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : POSITION0;
    float3 WorldNormal : NORMAL;
    float3 WorldTangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 ShadowPosition : TEXCOORD1;
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
    
    output.ShadowPosition =
    mul(
        float4(input.Position, 1.0f),
        gWorldLightViewProj
    );

    // Correct for the current uniform object scaling.
    output.WorldNormal =
        normalize(
            mul(
                input.Normal,
                (float3x3) gWorld
            )
        );
    output.WorldTangent =
    normalize(
        mul(
            input.Tangent,
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

    // Sample tangent-space normal.
    // Texture stores values in [0, 1], so convert them to [-1, 1].
    float3 tangentNormal =
        gNormalMap.Sample(
            gSampler,
            input.UV
        ).xyz;

    tangentNormal = tangentNormal * 2.0f - 1.0f;


    // Build orthonormal tangent basis.
    float3 normal =
    normalize(
        input.WorldNormal
    );

    float3 tangent =
    normalize(
        input.WorldTangent
        -
        normal *
        dot(
            input.WorldTangent,
            normal
        )
    );

    float3 bitangent =
    normalize(
        cross(
            normal,
            tangent
        )
    );


    // Tangent space -> world space.
    float3x3 TBN =
    float3x3(
        tangent,
        bitangent,
        normal
    );

    normal =
    normalize(
        mul(
            tangentNormal,
            TBN
        )
    );

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

    float shadowFactor =
    CalculateShadowFactor(
        input.ShadowPosition
    );


    float3 directLighting =
    gLightColor *
    gLightIntensity *
    diffuseStrength;


// Ambient light is not shadowed.
    float3 lighting =
    gAmbientStrength
    +
    shadowFactor *
    directLighting;


    float3 specularLighting =
    shadowFactor *
    gLightColor *
    gSpecularStrength *
    specularStrength;


    float3 finalColor =
    albedo.rgb * lighting
    +
    specularLighting;

    return float4(
        finalColor,
        albedo.a
    );
}