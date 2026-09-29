cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
    float4x4 gWorldLightViewProj;

    float3 gCameraPosition;
    float gPadding;
};

Texture2D gBillboardTexture : register(t2);
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
    // Billboard point positions are currently stored directly
    // in world space. The billboard object therefore uses an
    // identity World matrix.

    float3 center =
        input[0].Position;

    float3 toCamera =
        normalize(
            gCameraPosition -
            center
        );


    // Prevent instability when the camera is almost directly
    // above or below the billboard.
    float3 upGuide =
        abs(toCamera.y) > 0.99f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(0.0f, 1.0f, 0.0f);


    float3 right =
        normalize(
            cross(
                upGuide,
                toCamera
            )
        );

    float3 up =
        normalize(
            cross(
                toCamera,
                right
            )
        );


    const float halfWidth = 0.30f;
    const float halfHeight = 0.45f;


    float3 bottomLeft =
        center
        - right * halfWidth;

    float3 topLeft =
        center
        - right * halfWidth
        + up * (2.0f * halfHeight);

    float3 bottomRight =
        center
        + right * halfWidth;

    float3 topRight =
        center
        + right * halfWidth
        + up * (2.0f * halfHeight);


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
    return float4(
        1.0f,
        0.0f,
        1.0f,
        1.0f
    );
}