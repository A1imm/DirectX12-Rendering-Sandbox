cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
    float4x4 gWorldLightViewProj;

    float3 gCameraPosition;
    float gPadding;
};

Texture2D gBillboardTexture : register(t5);
SamplerState gSampler : register(s0);


struct VertexInput
{
    float3 Position : POSITION;
};


struct VertexOutput
{
    float3 Position : POSITION;
};


VertexOutput VS_Main(VertexInput input)
{
    VertexOutput output;

    output.Position = input.Position;

    return output;
}


struct GeometryOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};


[maxvertexcount(4)]
void GS_Main(
    point VertexOutput input[1],
    inout TriangleStream<GeometryOutput> outputStream)
{
    float3 center =
        input[0].Position;

    // Direction to camera, but only in XZ plane.
    // This keeps the billboard vertical.
    float3 toCamera =
        float3(
            gCameraPosition.x - center.x,
            0.0f,
            gCameraPosition.z - center.z
        );

    toCamera =
        normalize(toCamera);

    const float3 worldUp =
        float3(
            0.0f,
            1.0f,
            0.0f
        );

    float3 right =
        normalize(
            cross(
                worldUp,
                toCamera
            )
        );

    const float halfWidth = 0.30f;
    const float height = 0.90f;

    // center represents the bottom-center of the billboard.
    float3 bottomLeft =
        center -
        right * halfWidth;

    float3 bottomRight =
        center +
        right * halfWidth;

    float3 topLeft =
        bottomLeft +
        worldUp * height;

    float3 topRight =
        bottomRight +
        worldUp * height;


    GeometryOutput output;


    // Bottom-left
    output.Position =
        mul(
            float4(bottomLeft, 1.0f),
            gWorldViewProj
        );

    output.UV =
        float2(0.0f, 1.0f);

    outputStream.Append(output);


    // Top-left
    output.Position =
        mul(
            float4(topLeft, 1.0f),
            gWorldViewProj
        );

    output.UV =
        float2(0.0f, 0.0f);

    outputStream.Append(output);


    // Bottom-right
    output.Position =
        mul(
            float4(bottomRight, 1.0f),
            gWorldViewProj
        );

    output.UV =
        float2(1.0f, 1.0f);

    outputStream.Append(output);


    // Top-right
    output.Position =
        mul(
            float4(topRight, 1.0f),
            gWorldViewProj
        );

    output.UV =
        float2(1.0f, 0.0f);

    outputStream.Append(output);

    outputStream.RestartStrip();
}


float4 PS_Main(
    GeometryOutput input) : SV_Target
{
    float4 color =
        gBillboardTexture.Sample(
            gSampler,
            input.UV
        );

    clip(color.a - 0.30f);

    return color;
}