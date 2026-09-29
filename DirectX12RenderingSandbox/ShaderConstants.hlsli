#ifndef SHADER_CONSTANTS_HLSLI
#define SHADER_CONSTANTS_HLSLI

// ============================================================
// TERRAIN
// ============================================================

// Macro is used because maxtessfactor() requires
// a compile-time constant.
#define TERRAIN_MAX_TESS_FACTOR 64.0f

static const float kTerrainMinTessFactor = 4.0f;
static const float kTerrainTessellationDistance = 10.0f;
static const float kTerrainHeightScale = 0.30f;


// ============================================================
// SHADOWS
// ============================================================

static const float kShadowReceiverBias = 0.001f;


// ============================================================
// BILLBOARDS
// ============================================================

static const float kBillboardHalfWidth = 0.30f;
static const float kBillboardHeight = 0.90f;
static const float kBillboardAlphaCutoff = 0.30f;

#endif