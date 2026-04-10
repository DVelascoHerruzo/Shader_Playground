#include "Terrain.h"

void Terrain::Create(ID3D11Device* device) {
    BuildGrid(device);

    // ---- Main terrain shader (VS + HS + DS + PS) ----
    m_mainShader.Create(device,
        L"shaders/terrain_vs.hlsl", "VSMain",
        L"shaders/terrain_ps.hlsl", "PSMain",
        L"shaders/terrain_hs.hlsl", "HSMain",
        L"shaders/terrain_ds.hlsl", "DSMain");

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_mainShader.CreateInputLayout(device, elems, 2);

    // ---- Shadow terrain shader (VS + HS + DS, no PS) ----
    m_shadowShader.Create(device,
        L"shaders/terrain_vs.hlsl", "VSMain",
        nullptr, nullptr,
        L"shaders/terrain_hs.hlsl", "HSMain",
        L"shaders/shadow_ds.hlsl",  "DSMain");
    m_shadowShader.CreateInputLayout(device, elems, 2);

    // ---- Terrain CB ----
    m_terrainCB.Create(device);

    // ---- Rasterizer states ----
    D3D11_RASTERIZER_DESC rd{};
    rd.AntialiasedLineEnable = FALSE;
    rd.CullMode              = D3D11_CULL_BACK;
    rd.DepthClipEnable       = TRUE;
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.FrontCounterClockwise = TRUE;   // tessellator emits CCW triangles for the top face
    HR(device->CreateRasterizerState(&rd, &m_rsSolid));

    rd.FillMode = D3D11_FILL_WIREFRAME;
    rd.CullMode = D3D11_CULL_NONE;
    HR(device->CreateRasterizerState(&rd, &m_rsWireframe));
}

void Terrain::BuildGrid(ID3D11Device* device) {
    // GRID x GRID patches, each 4 control points (quad corners)
    // Total vertices = (GRID+1)^2, indices = GRID^2 * 4
    const int VN = GRID + 1;
    std::vector<TerrainVertex> verts;
    verts.reserve(VN * VN);

    for (int y = 0; y <= GRID; y++) {
        for (int x = 0; x <= GRID; x++) {
            float u = (float)x / GRID;
            float v = (float)y / GRID;
            verts.push_back({
                { (u - 0.5f) * TERRAIN_WORLD_SIZE, (v - 0.5f) * TERRAIN_WORLD_SIZE },
                { u, v }
            });
        }
    }

    // Index buffer: for each patch, 4 corners in order: BL, BR, TL, TR
    std::vector<uint32_t> indices;
    indices.reserve(GRID * GRID * 4);
    for (int y = 0; y < GRID; y++) {
        for (int x = 0; x < GRID; x++) {
            indices.push_back(y * VN + x);
            indices.push_back(y * VN + x + 1);
            indices.push_back((y+1) * VN + x);
            indices.push_back((y+1) * VN + x + 1);
        }
    }
    m_indexCount = (UINT)indices.size();

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(verts.size() * sizeof(TerrainVertex));
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr{ verts.data() };
    HR(device->CreateBuffer(&bd, &sr, &m_vb));

    bd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sr.pSysMem   = indices.data();
    HR(device->CreateBuffer(&bd, &sr, &m_ib));
}

void Terrain::DrawPatches(ID3D11DeviceContext* ctx) const {
    UINT stride = sizeof(TerrainVertex), offset = 0;
    ID3D11Buffer* vb = m_vb.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    ctx->DrawIndexed(m_indexCount, 0, 0);
}

void Terrain::SetWireframe(ID3D11DeviceContext* ctx, bool enable) const {
    ctx->RSSetState(enable ? m_rsWireframe.Get() : m_rsSolid.Get());
}

void Terrain::HotReloadShaders(ID3D11Device* device) {
    m_mainShader.HotReload(device);
    m_shadowShader.HotReload(device);
}
