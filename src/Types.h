#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <cstdint>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ---------------------------------------------------------------------------
//  Error Helpers
// ---------------------------------------------------------------------------
inline void CheckHR(HRESULT hr, const char* msg) {
    if (FAILED(hr)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s\nHRESULT: 0x%08X", msg, (unsigned)hr);
        MessageBoxA(nullptr, buf, "Fatal Error", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
}
#define CHECK_HR(hr, msg) CheckHR(hr, msg)
#define HR(expr)          CHECK_HR((expr), #expr)

// ---------------------------------------------------------------------------
//  Constant Buffer Structs  (must match HLSL register layouts exactly)
//  All structs are padded to 16-byte boundaries.
// ---------------------------------------------------------------------------

struct alignas(16) CameraData {
    XMFLOAT4X4 view;
    XMFLOAT4X4 proj;
    XMFLOAT4X4 viewProj;
    XMFLOAT4X4 invProj;
    XMFLOAT4X4 invViewProj;
    XMFLOAT3   camPos;       float nearPlane;
    XMFLOAT3   camDir;       float farPlane;
    float screenWidth;
    float screenHeight;
    float pad[2];
};

struct alignas(16) SunLightData {
    XMFLOAT3 dirToSun;       float sunIntensity;
    XMFLOAT3 sunColor;       float ambientIntensity;
    XMFLOAT3 ambientColor;   float fogDensity;
    XMFLOAT3 fogColor;       float timeOfDay;
    XMFLOAT3 zenithColor;    float pad0;
    XMFLOAT3 horizonColor;   float pad1;
};

struct alignas(16) ShadowData {
    XMFLOAT4X4 lightViewProj[3];
    float cascadeSplits[4];    // xyz = 3 splits, w = unused
    int   cascadeIndex;        // which cascade to render (set per shadow pass)
    float pad[3];
};

struct alignas(16) TerrainData {
    float heightScale;
    float terrainSize;
    int   gridSize;
    float uvTile;
    XMFLOAT4 thresholds;       // x=sand h, y=grass h, z=rock h, w=snow h (0-1)
    float slopeThreshold;
    float pad[3];
};

struct alignas(16) WaterData {
    float seaLevel;
    float waveSpeed;
    float waveStrength;
    float foamThreshold;
    float time;
    float ior;
    float reflectBias;
    float pad;
};

struct alignas(16) SSAOData {
    XMFLOAT4 samples[64];     // xyz = hemisphere sample, w = unused
    float radius;
    float bias;
    float intensity;
    float pad;
};

struct alignas(16) PostProcessData {
    float exposure;          // row 1
    int   ssaoEnabled;
    int   fxaaEnabled;
    int   underwaterEnabled;
    float bloomThreshold;    // row 2 — luminance threshold for bloom (e.g. 1.0)
    float bloomIntensity;    // bloom additive blend strength
    float godRayIntensity;   // screen-space light shaft intensity
    float lensFlareIntensity;// lens flare + starburst intensity
    float ssgiIntensity;     // row 3 — indirect bounce contribution
    float pad1[3];
};

struct alignas(16) ClipPlaneData {
    XMFLOAT4 clipPlane;       // ax + by + cz + d = 0
};

struct alignas(16) FoliageData {
    float time;
    float windStrength;
    float windFrequency;
    float drawDistance;       // grass fade-out distance in world metres
    float rotationOffset;     // extra Y-rotation for this draw pass (radians)
    float pad0, pad1, pad2;
};

struct alignas(16) RayTraceData {
    int   enabled;
    int   reflectEnabled;
    int   refractEnabled;
    int   stepsPerRay;
    float maxDistance;
    int   shadowSteps;   // steps for terrain RT shadow ray march
    float pad1;
    float pad2;
};

// ---------------------------------------------------------------------------
//  Simple vertex types
// ---------------------------------------------------------------------------
struct TerrainVertex {
    XMFLOAT2 posXZ;
    XMFLOAT2 uv;
};

struct WaterVertex {
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};

// Per-vertex unit cross-quad for grass rendering (2 crossed quads = 12 vertices)
struct GrassVertex {
    XMFLOAT3 localPos;    // local-space position, Y in [0,1] = base to tip
    XMFLOAT2 uv;          // u=0..1 across width, v=0..1 from base to tip
    XMFLOAT3 localNormal; // geometric normal of this quad face
};

// Per-instance grass clump data (32 bytes, D3D11_INPUT_PER_INSTANCE_DATA)
struct FoliageInstanceData {
    XMFLOAT2 posXZ;    // world-space centre (X, Z)
    float    worldY;  // terrain surface Y
    float    rotation; // Y-axis rotation (radians)
    float    spread;  // XZ half-width of the clump (metres)
    float    height;  // blade height (metres)
    float    tint;    // random [-1, 1] colour variation
    float    pad;
};

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------
constexpr int   TERRAIN_GRID_SIZE  = 64;      // NxN patches
constexpr int   HEIGHTMAP_SIZE     = 2049;     // (2^10+1) heightmap — 1.95m/texel on a 2000m terrain
constexpr float TERRAIN_WORLD_SIZE = 2000.0f;
constexpr float TERRAIN_HEIGHT     = 220.0f;
constexpr float SEA_LEVEL          = 0.0f;
constexpr int   SHADOW_MAP_SIZE    = 2048;
constexpr int   NUM_CASCADES       = 3;
constexpr float CAMERA_NEAR        = 0.3f;
constexpr float CAMERA_FAR         = 3000.0f;
