#include "Foliage.h"
#include <random>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  Unit cross-quad mesh
//  Two quads meeting at the centre column, 6 vertices each (2 triangles).
//  Local space: Y = 0 at root, Y = 1 at tip; XZ in [-0.5, 0.5].
//  The instance VS scales by (spread, height, spread) and rotates around Y.
// ---------------------------------------------------------------------------
void Foliage::BuildUnitMesh(ID3D11Device* device)
{
    constexpr float W = 0.5f;   // half-width

    // Single flat quad, spanning local X (width), standing up in Y (height).
    // Normal = +Z so rotation by instRot turns it to face any direction.
    // Draw() renders this quad THREE TIMES at +0°, +60°, -60° Y-rotation
    // (matching the reference 0°, +50°, -50° three-pass scheme) to form a
    // dense 3-plane grass cluster per blade point — exactly like BillboardGrass.
    const GrassVertex verts[6] = {
        { {-W, 0, 0}, {0, 0}, {0, 0, 1} },
        { { W, 0, 0}, {1, 0}, {0, 0, 1} },
        { {-W, 1, 0}, {0, 1}, {0, 0, 1} },
        { { W, 0, 0}, {1, 0}, {0, 0, 1} },
        { { W, 1, 0}, {1, 1}, {0, 0, 1} },
        { {-W, 1, 0}, {0, 1}, {0, 0, 1} },
    };

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(verts);
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr{ verts };
    HR(device->CreateBuffer(&bd, &sr, &m_unitVB));
}

// ---------------------------------------------------------------------------
void Foliage::Create(ID3D11Device* device)
{
    BuildUnitMesh(device);

    m_shader.Create(device,
        L"shaders/grass_vs.hlsl", "VSMain",
        L"shaders/grass_ps.hlsl", "PSMain");

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        // Slot 0: per-vertex unit mesh
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        // Slot 1: per-instance data (stride = sizeof FoliageInstanceData = 32)
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // posXZ
        { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       1,  8, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // worldY
        { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // rotation
        { "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // spread
        { "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT,       1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // height
        { "TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT,       1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // tint
    };
    m_shader.CreateInputLayout(device, elems, 9);

    m_foliageCB.Create(device);

    // No-cull rasterizer so both quad faces are visible
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.CullMode              = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable       = TRUE;
    HR(device->CreateRasterizerState(&rd, &m_rsCullNone));
}

// ---------------------------------------------------------------------------
void Foliage::Rebuild(ID3D11Device* device, const TerrainGenerator& gen,
                     float tSandGrass, float tGrassRock, float slopeThreshold)
{
    const auto& hmap = gen.GetHeightmap();
    if (hmap.empty()) return;

    const int   N    = TerrainGenerator::N;      // 2049
    const float sz   = TERRAIN_WORLD_SIZE;
    const float h    = TERRAIN_HEIGHT;
    const float worldPerTexel = sz / (float)(N - 1);

    // ------------------------------------------------------------------
    // Uniform grid placement across the full terrain.
    // 2m grid over the full 2000×2000m = 1000×1000 = 1M cells.
    // ~30% grass biome ≈ 300k instances, well within the 500k cap.
    // Covers the entire terrain regardless of where the biome lands.
    // ------------------------------------------------------------------
    const float gridSpacing = 0.5f;
    const float jitter      = gridSpacing * 0.45f;
    const float zoneHalf    = sz * 0.5f;   // = 1000m, full terrain
    const float zoneSize    = sz;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> distJitter(-jitter,   jitter);
    std::uniform_real_distribution<float> distAngle (0.0f, 6.28318f);
    std::uniform_real_distribution<float> distVar   (-1.0f, 1.0f);
    std::uniform_real_distribution<float> distNorm  (0.0f,  1.0f);

    std::vector<FoliageInstanceData> instances;
    instances.reserve(maxInstances);

    const int Nx = (int)(zoneSize / gridSpacing);
    const int Nz = (int)(zoneSize / gridSpacing);

    for (int xi = 0; xi < Nx && (int)instances.size() < maxInstances; ++xi)
    {
        for (int zi = 0; zi < Nz && (int)instances.size() < maxInstances; ++zi)
        {
            // Grid centre + jitter, offset so (0,0) is terrain/zone centre
            float worldX = (xi * gridSpacing + distJitter(rng)) - zoneHalf;
            float worldZ = (zi * gridSpacing + distJitter(rng)) - zoneHalf;

            // Clamp to terrain then convert to heightmap texel index
            float normX = std::clamp((worldX + sz * 0.5f) / sz, 0.0f, 1.0f);
            float normZ = std::clamp((worldZ + sz * 0.5f) / sz, 0.0f, 1.0f);
            int ix = std::min((int)(normX * (N - 1)), N - 2);
            int iz = std::min((int)(normZ * (N - 1)), N - 2);

            float hC = hmap[iz * N + ix];

            // Grass biome: raw heightmap value [0,1] compared to biome thresholds.
            // heightNorm is the 0-1 terrain height used for biome classification.
            // inst.worldY uses the full hC so the blade sits ON the terrain surface.
            float heightNorm = hC - 0.3f;
            if (heightNorm < tSandGrass || heightNorm > tGrassRock) continue;

            // Slope filter: central difference normal Y-component
            float hR = hmap[iz * N + std::min(ix + 1, N - 1)];
            float hU = hmap[std::min(iz + 1, N - 1) * N + ix];
            float dhdx = (hR - hC) * h / worldPerTexel;
            float dhdz = (hU - hC) * h / worldPerTexel;
            float slopeY = 1.0f / std::sqrtf(1.0f + dhdx * dhdx + dhdz * dhdz);
            if (slopeY < slopeThreshold) continue;

            FoliageInstanceData inst{};
            inst.posXZ    = { worldX, worldZ };
            inst.worldY   = (hC - 0.3f) * h;   // match terrain_ds: (h-0.3)*heightScale
            inst.rotation = distAngle(rng);
            // Height variation: reference pos.w = lerp(0.3, 0.6, noise)
            // giving height multiplier 0.8-1.1×.  We use a wider range for
            // more visual variety at the same density.
            inst.spread   = 0.06f + distNorm(rng) * 0.08f;  // 0.06–0.14 m  (blade width)
            inst.height   = 3.50f + distNorm(rng) * 0.35f;  // 2.20–2.55 m  (short dense grass)
            inst.tint     = distVar(rng);
            inst.pad      = 0.0f;
            instances.push_back(inst);
        }
    }

    m_instanceCount = (UINT)instances.size();
    m_instanceVB.Reset();

    if (m_instanceCount == 0) return;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(instances.size() * sizeof(FoliageInstanceData));
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr{ instances.data() };
    HR(device->CreateBuffer(&bd, &sr, &m_instanceVB));
}

// ---------------------------------------------------------------------------
void Foliage::Draw(ID3D11DeviceContext* ctx, float time,
                   float windStrength, float windFrequency)
{
    if (!enabled || m_instanceCount == 0 || !m_instanceVB) return;

    // Save and override rasterizer state (two-sided, no cull)
    ComPtr<ID3D11RasterizerState> prevRS;
    ctx->RSGetState(&prevRS);
    ctx->RSSetState(m_rsCullNone.Get());

    // Vertex buffers: slot0=unit mesh (single quad), slot1=instance transforms
    ID3D11Buffer* vbs[2]     = { m_unitVB.Get(), m_instanceVB.Get() };
    UINT          strides[2] = { sizeof(GrassVertex), sizeof(FoliageInstanceData) };
    UINT          offsets[2] = { 0, 0 };
    ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_shader.Bind(ctx);

    // Bind grass billboard textures — t0=albedo/alpha, t1=normal map
    ID3D11ShaderResourceView* grassSRVs[2] = { m_texAlbedo, m_texNormal };
    ctx->PSSetShaderResources(0, 2, grassSRVs);

    // Single-pass camera-facing billboard — rotation is computed per-blade in the VS.
    FoliageData fd{};
    fd.time          = time;
    fd.windStrength  = windStrength;
    fd.windFrequency = windFrequency;
    fd.drawDistance  = drawDistance;
    fd.rotationOffset = 0.0f;
    m_foliageCB.Update(ctx, fd);
    m_foliageCB.VSBind(ctx, 8);
    ctx->DrawInstanced(6, m_instanceCount, 0, 0);

    // Unbind
    ID3D11ShaderResourceView* nullSRVs[2] = {};
    ctx->PSSetShaderResources(0, 2, nullSRVs);
    ctx->RSSetState(prevRS.Get());
}

// ---------------------------------------------------------------------------
void Foliage::HotReloadShaders(ID3D11Device* device)
{
    m_shader.HotReload(device);
}
