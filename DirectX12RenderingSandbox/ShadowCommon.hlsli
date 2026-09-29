#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

#include "ShaderConstants.hlsli"

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
                    projected.z -
                    kShadowReceiverBias
                );
        }
    }

    return shadowFactor / 9.0f;
}

#endif