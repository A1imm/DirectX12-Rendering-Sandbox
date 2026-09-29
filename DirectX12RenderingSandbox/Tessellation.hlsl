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

struct VertexData
{
	float3 PosL  : POSITION;
	float2 UV : TEXCOORD;
};

/*----------------VERTEX SHADER------------------------------------------*/
VertexData VS_Main(VertexData vin)
{
	VertexData vout;
	vout.PosL = vin.PosL;
	vout.UV = vin.UV;
	return vout;
}

struct PatchTess
{
	float EdgeTess[4] : SV_TessFactor;
	float InsideTess[2] : SV_InsideTessFactor;
};

/*----------------CONSTANT HULL SHADER------------------------------------------*/
PatchTess ConstantHS(InputPatch<VertexData, 4> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess patchTess;
    float3 objectCenter =
    mul(
        float4(0.0f, 0.0f, 0.0f, 1.0f),
        gWorld
    ).xyz;

    float distanceToCamera = distance(gCameraPosition, objectCenter);

    float tessFactor = lerp(64.0f, 4.0f, saturate(distanceToCamera / 10.0f));

    patchTess.EdgeTess[0] = tessFactor;
    patchTess.EdgeTess[1] = tessFactor;
    patchTess.EdgeTess[2] = tessFactor;
    patchTess.EdgeTess[3] = tessFactor;
    patchTess.InsideTess[0] = tessFactor;
    patchTess.InsideTess[1] = tessFactor;
    
    return patchTess;
}

struct HullOut
{
	float3 PosL : POSITION;
	float2 uv : TEXCOORD0;
};

/*----------------HULL SHADER------------------------------------------*/
[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
HullOut HS_Main(InputPatch<VertexData, 4> p, uint i : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    HullOut hout;
    hout.PosL = p[i].PosL;
    hout.uv = p[i].UV;
    return hout;
}

struct DomainOut
{
	float4 PosH : SV_POSITION;
    float3 WorldPosition : POSITION0;
    float3 WorldNormal : NORMAL;
	float2 uv : TEXCOORD0;
    float4 ShadowPosition : TEXCOORD1;
};

/*----------------DOMAIN SHADER------------------------------------------*/
Texture2D gHeightMap : register(t1);
Texture2D gTerrainTexture : register(t2);
Texture2D<float> gShadowMap : register(t4);

SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

[domain("quad")]
DomainOut DS_Main(
    PatchTess patchTess,
    float2 uv : SV_DomainLocation,
    const OutputPatch<HullOut, 4> quad)
{
    DomainOut dout;

    // Interpolate local position across the quad patch.
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

    float3 p =
        lerp(
            v1,
            v2,
            uv.y
        );


    // Interpolate texture coordinates.
    float2 uvV1 =
        lerp(
            quad[0].uv,
            quad[1].uv,
            uv.x
        );

    float2 uvV2 =
        lerp(
            quad[3].uv,
            quad[2].uv,
            uv.x
        );

    dout.uv =
        lerp(
            uvV1,
            uvV2,
            uv.y
        );


    // =====================================================
    // HEIGHT DISPLACEMENT
    // =====================================================

    const float heightScale = 0.3f;

    float height =
        gHeightMap.SampleLevel(
            gSampler,
            dout.uv,
            0
        ).r;

    p.y += height * heightScale;


    // =====================================================
    // NORMAL FROM HEIGHT MAP
    // =====================================================

    uint textureWidth;
    uint textureHeight;

    gHeightMap.GetDimensions(
        textureWidth,
        textureHeight
    );

    float2 texelSize =
        float2(
            1.0f / textureWidth,
            1.0f / textureHeight
        );

    float heightUPositive =
        gHeightMap.SampleLevel(
            gSampler,
            dout.uv + float2(texelSize.x, 0.0f),
            0
        ).r;

    float heightUNegative =
        gHeightMap.SampleLevel(
            gSampler,
            dout.uv - float2(texelSize.x, 0.0f),
            0
        ).r;

    float heightVPositive =
        gHeightMap.SampleLevel(
            gSampler,
            dout.uv + float2(0.0f, texelSize.y),
            0
        ).r;

    float heightVNegative =
        gHeightMap.SampleLevel(
            gSampler,
            dout.uv - float2(0.0f, texelSize.y),
            0
        ).r;


    // Texture U corresponds to local Z.
    float3 tangentZ =
        float3(
            0.0f,
            (heightUPositive - heightUNegative)
                * heightScale,
            4.0f * texelSize.x
        );

    // Texture V is inverted relative to local X.
    float3 tangentX =
        float3(
            4.0f * texelSize.y,
            (heightVNegative - heightVPositive)
                * heightScale,
            0.0f
        );

    float3 localNormal =
        normalize(
            cross(
                tangentZ,
                tangentX
            )
        );


    // =====================================================
    // WORLD SPACE OUTPUT
    // =====================================================

    float4 worldPosition =
        mul(
            float4(p, 1.0f),
            gWorld
        );

    dout.WorldPosition =
        worldPosition.xyz;

    // Correct for our current uniform terrain scaling.
    dout.WorldNormal =
        normalize(
            mul(
                localNormal,
                (float3x3) gWorld
            )
        );

    dout.PosH =
        mul(
            float4(p, 1.0f),
            gWorldViewProj
        );
    
    dout.ShadowPosition =
    mul(
        float4(p, 1.0f),
        gWorldLightViewProj
    );

    return dout;
}

float CalculateShadowFactor(
    float4 shadowPosition)
{
    float3 projected =
        shadowPosition.xyz /
        shadowPosition.w;

    float2 shadowUV;

    shadowUV.x =
        projected.x * 0.5f + 0.5f;

    shadowUV.y =
        -projected.y * 0.5f + 0.5f;

    if (projected.z <= 0.0f ||
        projected.z >= 1.0f)
    {
        return 1.0f;
    }

    if (shadowUV.x < 0.0f ||
        shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f ||
        shadowUV.y > 1.0f)
    {
        return 1.0f;
    }

    uint shadowWidth;
    uint shadowHeight;

    gShadowMap.GetDimensions(
        shadowWidth,
        shadowHeight
    );

    float2 texelSize =
        1.0f /
        float2(
            shadowWidth,
            shadowHeight
        );

    const float shadowBias = 0.001f;

    float shadowFactor = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset =
                float2(x, y) *
                texelSize;

            shadowFactor +=
                gShadowMap.SampleCmpLevelZero(
                    gShadowSampler,
                    shadowUV + offset,
                    projected.z - shadowBias
                );
        }
    }

    return shadowFactor / 9.0f;
}

/*----------------PIXEL SHADER------------------------------------------*/
float4 PS_Main(DomainOut input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 albedo =
        gTerrainTexture.Sample(
            gSampler,
            input.uv
        );

    float3 normal =
    normalize(
        input.WorldNormal
    );

    if (!isFrontFace)
    {
        normal = -normal;
    }

    float3 lightDirection =
        normalize(
            gLightDirection
        );

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

    float shadowFactor =
    CalculateShadowFactor(
        input.ShadowPosition
    );


    float3 directLighting =
    gLightColor *
    gLightIntensity *
    diffuseStrength;


    float3 lighting =
    gAmbientStrength
    +
    shadowFactor *
    directLighting;


    float3 finalColor =
    albedo.rgb *
    lighting;

    return float4(
        finalColor,
        albedo.a
    );
}