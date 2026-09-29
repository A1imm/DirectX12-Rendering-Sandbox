#include "ShaderConstants.hlsli"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
    float4x4 gWorldLightViewProj;

    float3 gCameraPosition;
    float gPadding;
};


// ============================================================
// CUBE SHADOW PASS
// ============================================================

struct BasicVertexInput
{
    float3 Position : POSITION;
};

struct BasicShadowOutput
{
    float4 Position : SV_POSITION;
};

BasicShadowOutput VS_BasicShadow(
    BasicVertexInput input)
{
    BasicShadowOutput output;

    output.Position =
        mul(
            float4(input.Position, 1.0f),
            gWorldLightViewProj
        );

    return output;
}


// ============================================================
// TERRAIN SHADOW PASS
// ============================================================

Texture2D gHeightMap : register(t1);
SamplerState gSampler : register(s0);

struct TerrainVertexData
{
    float3 PosL : POSITION;
    float2 UV : TEXCOORD;
};

TerrainVertexData VS_TerrainShadow(
    TerrainVertexData input)
{
    TerrainVertexData output;

    output.PosL = input.PosL;
    output.UV = input.UV;

    return output;
}


struct PatchTess
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};


PatchTess ConstantHS_Shadow(
    InputPatch<TerrainVertexData, 4> patch,
    uint patchID : SV_PrimitiveID)
{
    PatchTess patchTess;

    float3 objectCenter =
        mul(
            float4(
                0.0f,
                0.0f,
                0.0f,
                1.0f
            ),
            gWorld
        ).xyz;

    // Use the same tessellation rule as the main pass,
    // so the shadow geometry matches the visible geometry.
    float distanceToCamera =
        distance(
            gCameraPosition,
            objectCenter
        );

    float tessFactor =
        lerp(
            TERRAIN_MAX_TESS_FACTOR,
            kTerrainMinTessFactor,
            saturate(
                distanceToCamera /
                kTerrainTessellationDistance
            )
        );

    patchTess.EdgeTess[0] = tessFactor;
    patchTess.EdgeTess[1] = tessFactor;
    patchTess.EdgeTess[2] = tessFactor;
    patchTess.EdgeTess[3] = tessFactor;

    patchTess.InsideTess[0] = tessFactor;
    patchTess.InsideTess[1] = tessFactor;

    return patchTess;
}


struct TerrainHullOut
{
    float3 PosL : POSITION;
    float2 UV : TEXCOORD0;
};


[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("ConstantHS_Shadow")]
[maxtessfactor(TERRAIN_MAX_TESS_FACTOR)]
TerrainHullOut HS_TerrainShadow(
    InputPatch<TerrainVertexData, 4> patch,
    uint index : SV_OutputControlPointID,
    uint patchID : SV_PrimitiveID)
{
    TerrainHullOut output;

    output.PosL =
        patch[index].PosL;

    output.UV =
        patch[index].UV;

    return output;
}


struct TerrainShadowOutput
{
    float4 Position : SV_POSITION;
};


[domain("quad")]
TerrainShadowOutput DS_TerrainShadow(
    PatchTess patchTess,
    float2 uv : SV_DomainLocation,
    const OutputPatch<TerrainHullOut, 4> quad)
{
    TerrainShadowOutput output;

    float3 v1 =
        lerp(
            quad[0].PosL,
            quad[1].PosL,
            uv.x
        );

    float3 v2 =
        lerp(
            quad[3].PosL,
            quad[2].PosL,
            uv.x
        );

    float3 position =
        lerp(
            v1,
            v2,
            uv.y
        );


    float2 uvV1 =
        lerp(
            quad[0].UV,
            quad[1].UV,
            uv.x
        );

    float2 uvV2 =
        lerp(
            quad[3].UV,
            quad[2].UV,
            uv.x
        );

    float2 terrainUV =
        lerp(
            uvV1,
            uvV2,
            uv.y
        );

    float height =
        gHeightMap.SampleLevel(
            gSampler,
            terrainUV,
            0
        ).r;

    position.y +=
        height * kTerrainHeightScale;


    output.Position =
        mul(
            float4(position, 1.0f),
            gWorldLightViewProj
        );

    return output;
}