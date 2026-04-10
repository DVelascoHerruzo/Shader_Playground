#include "TerrainGenerator.h"
#include "FastNoiseLite.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <DirectXPackedVector.h>
using namespace DirectX::PackedVector;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
float TerrainGenerator::BilinearHeight(const std::vector<float>& h, int N, float x, float y) {
    x = std::clamp(x, 0.0f, (float)(N - 2));
    y = std::clamp(y, 0.0f, (float)(N - 2));
    int ix = (int)x, iy = (int)y;
    float fx = x - ix, fy = y - iy;
    float v00 = h[iy * N + ix];
    float v10 = h[iy * N + ix + 1];
    float v01 = h[(iy + 1) * N + ix];
    float v11 = h[(iy + 1) * N + ix + 1];
    return (v00 * (1-fx) + v10 * fx) * (1-fy) + (v01 * (1-fx) + v11 * fx) * fy;
}

void TerrainGenerator::BilinearDeposit(std::vector<float>& h, int N, float x, float y, float amount) {
    x = std::clamp(x, 0.0f, (float)(N - 2));
    y = std::clamp(y, 0.0f, (float)(N - 2));
    int ix = (int)x, iy = (int)y;
    float fx = x - ix, fy = y - iy;
    h[iy * N + ix]             += amount * (1-fx) * (1-fy);
    h[iy * N + ix + 1]         += amount *   fx   * (1-fy);
    h[(iy+1) * N + ix]         += amount * (1-fx) *   fy;
    h[(iy+1) * N + ix + 1]     += amount *   fx   *   fy;
}

// ---------------------------------------------------------------------------
//  Destructor — wait for any in-progress background thread before releasing
// ---------------------------------------------------------------------------
TerrainGenerator::~TerrainGenerator() {
    m_stopRequested.store(true);   // ask the thread to bail out early
    if (m_thread.joinable()) m_thread.join();
}

// ---------------------------------------------------------------------------
//  Kick off async generation
// ---------------------------------------------------------------------------
void TerrainGenerator::Generate(const Params& p, std::function<void()> onComplete) {
    if (m_running.load()) return;  // already running
    m_ready.store(false);
    m_running.store(true);
    m_progress.store(0.0f);
    m_stopRequested.store(false);   // clear any previous stop request
    params = p;

    if (m_thread.joinable()) m_thread.join();
    m_thread = std::thread(&TerrainGenerator::RunOnThread, this, p, onComplete);
}

// ---------------------------------------------------------------------------
//  Background thread: FBM + erosion
// ---------------------------------------------------------------------------
void TerrainGenerator::RunOnThread(Params p, std::function<void()> cb) {
    const int N  = TerrainGenerator::N;
    const int SN = N * std::max(1, p.simScale);   // simulation grid side length

    // High-res simulation buffer — freed / moved into m_heightmap after downsample
    m_simHeightmap.resize(SN * SN);
    auto& simH = m_simHeightmap;

    // --- 1. Domain-warped FBM via FastNoiseLite ---
    FastNoiseLite warpNoise, fbmNoise;
    warpNoise.SetSeed(p.seed + 100);
    warpNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    warpNoise.SetFrequency(p.frequency * 0.4f);
    warpNoise.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
    warpNoise.SetDomainWarpAmp(p.domainWarpAmp);

    fbmNoise.SetSeed(p.seed);
    fbmNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);  // Perlin FBM
    fbmNoise.SetFrequency(p.frequency);
    fbmNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    fbmNoise.SetFractalOctaves(p.octaves);
    fbmNoise.SetFractalLacunarity(p.lacunarity);
    fbmNoise.SetFractalGain(p.gain);

    // Generate FBM at sim resolution; map grid coords back to N-space so that
    // frequency and domain warp remain visually identical regardless of simScale.
    const float invScale = 1.0f / (float)p.simScale;
    float minH = 1e9f, maxH = -1e9f;
    for (int y = 0; y < SN; y++) {
        for (int x = 0; x < SN; x++) {
            float fx = (float)x * invScale, fy = (float)y * invScale;
            warpNoise.DomainWarp(fx, fy);
            float h = fbmNoise.GetNoise(fx, fy);
            simH[y * SN + x] = h;
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }
    // Normalize to [0..1]
    float range = maxH - minH;
    if (range < 1e-6f) range = 1.0f;
    for (auto& v : simH) v = (v - minH) / range;

    // Apply a power curve to sharpen mountains
    for (auto& v : simH) v = powf(v, 1.4f);

    m_progress.store(0.2f);

    // --- 2. Hydraulic Erosion (Lague algorithm) ---
    // Scale droplet count linearly with simScale to maintain the same erosion
    // density per render-resolution cell.
    const int maxLife = 64;
    const float inertia       = p.inertia;
    const float sedCap        = p.sedimentCap;
    const float erodeSpeed    = p.erosionSpeed;
    const float depositSpeed  = p.depositSpeed;
    const float evapSpeed     = p.evaporation;
    const float gravity       = p.gravity;
    const int   totalDroplets = p.erosionIters * std::max(1, p.simScale);

    std::mt19937 rng(p.seed + 999);
    std::uniform_real_distribution<float> dist(1.0f, (float)(SN - 2));

    for (int d = 0; d < totalDroplets; d++) {
        float px = dist(rng), py = dist(rng);
        float dirX = 0, dirY = 0;
        float speed = 1.0f, water = 1.0f, sediment = 0.0f;

        for (int life = 0; life < maxLife; life++) {
            int ix = (int)px, iy = (int)py;
            float fx = px - ix, fy = py - iy;

            // Compute gradient
            float h00 = simH[iy * SN + ix];
            float h10 = simH[iy * SN + ix + 1];
            float h01 = simH[(iy+1) * SN + ix];
            float h11 = simH[(iy+1) * SN + ix + 1];
            float gx  = (h10 - h00) * (1-fy) + (h11 - h01) * fy;
            float gy  = (h01 - h00) * (1-fx) + (h11 - h10) * fx;

            // Update direction (inertia + gradient)
            dirX = dirX * inertia - gx * (1.0f - inertia);
            dirY = dirY * inertia - gy * (1.0f - inertia);
            float len = sqrtf(dirX*dirX + dirY*dirY);
            if (len < 1e-6f) break;
            dirX /= len; dirY /= len;

            float newPx = px + dirX, newPy = py + dirY;
            if (newPx < 0 || newPx >= SN-1 || newPy < 0 || newPy >= SN-1) break;

            float oldH = BilinearHeight(simH, SN, px, py);
            float newH = BilinearHeight(simH, SN, newPx, newPy);
            float hDiff = newH - oldH;

            float capacity = std::max(-hDiff, 0.01f) * speed * water * sedCap;

            if (sediment > capacity || hDiff > 0) {
                // Deposit
                float dep = (hDiff > 0)
                    ? std::min(sediment, hDiff)
                    : (sediment - capacity) * depositSpeed;
                sediment -= dep;
                BilinearDeposit(simH, SN, px, py, dep);
            } else {
                // Erode
                float ero = std::min((capacity - sediment) * erodeSpeed, -hDiff);
                BilinearDeposit(simH, SN, px, py, -ero);
                sediment += ero;
            }

            speed    = sqrtf(std::max(0.0f, speed * speed + hDiff * gravity));
            water   *= (1.0f - evapSpeed);
            px = newPx; py = newPy;
        }

        if ((d & 0x3FF) == 0) {
            m_progress.store(0.2f + 0.7f * (float)d / (float)totalDroplets);
            if (m_stopRequested.load()) {
                m_running.store(false);
                return;
            }
        }
    }

    m_progress.store(0.9f);

    // --- 3. High-frequency detail layer (post-erosion so fine detail is preserved) ---
    if (p.detailAmplitude > 0.0f) {
        FastNoiseLite detailNoise;
        detailNoise.SetSeed(p.seed + 777);
        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        detailNoise.SetFrequency(p.frequency * p.detailFreqScale);
        detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        detailNoise.SetFractalOctaves(4);
        detailNoise.SetFractalLacunarity(2.2f);
        detailNoise.SetFractalGain(0.5f);
        for (int y = 0; y < SN; y++) {
            for (int x = 0; x < SN; x++) {
                float d = detailNoise.GetNoise((float)x * invScale, (float)y * invScale);
                simH[y * SN + x] = std::clamp(
                    simH[y * SN + x] + d * p.detailAmplitude, 0.0f, 1.0f);
            }
        }
    }

    // --- 3b. Downsample sim resolution -> render resolution ---
    if (p.simScale > 1) {
        // Box filter: each render cell averages a simScale x simScale block.
        m_heightmap.resize(N * N);
        const float invArea = 1.0f / (float)(p.simScale * p.simScale);
        for (int ry = 0; ry < N; ry++) {
            for (int rx = 0; rx < N; rx++) {
                float sum = 0.0f;
                const int baseY = ry * p.simScale;
                const int baseX = rx * p.simScale;
                for (int dy = 0; dy < p.simScale; dy++)
                    for (int dx = 0; dx < p.simScale; dx++)
                        sum += simH[(baseY + dy) * SN + (baseX + dx)];
                m_heightmap[ry * N + rx] = sum * invArea;
            }
        }
        // Free the high-res buffer now that we no longer need it.
        simH.clear();
        m_simHeightmap.shrink_to_fit();
    } else {
        // simScale == 1: zero-copy fast path, no behaviour change.
        m_heightmap = std::move(simH);
    }

    // --- 4. Compute normals (Sobel) ---
    BuildNormals();

    // --- 5. Compute Florinsky morphometric attributes (Slope, Aspect, kh, kv, kM, TWI) ---
    ComputeFlorinsky();

    m_progress.store(1.0f);
    m_ready.store(true);
    m_running.store(false);
    if (cb) cb();
}

// ---------------------------------------------------------------------------
//  Normals via central differences
// ---------------------------------------------------------------------------
void TerrainGenerator::BuildNormals() {
    const int N = TerrainGenerator::N;
    m_normals.resize(N * N);
    float invScale = 1.0f / (TERRAIN_WORLD_SIZE / (N - 1));

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int xl = std::max(0, x-1), xr = std::min(N-1, x+1);
            int yd = std::max(0, y-1), yu = std::min(N-1, y+1);
            float hL = m_heightmap[y * N + xl] * TERRAIN_HEIGHT;
            float hR = m_heightmap[y * N + xr] * TERRAIN_HEIGHT;
            float hD = m_heightmap[yd * N + x]  * TERRAIN_HEIGHT;
            float hU = m_heightmap[yu * N + x]  * TERRAIN_HEIGHT;
            float dx = (hR - hL) * invScale * 0.5f;
            float dz = (hU - hD) * invScale * 0.5f;
            XMVECTOR n = XMVector3Normalize(XMVectorSet(-dx, 1.0f, -dz, 0.0f));
            XMStoreFloat4(&m_normals[y * N + x], n);
        }
    }
}

// ---------------------------------------------------------------------------
//  Upload to GPU
// ---------------------------------------------------------------------------
void TerrainGenerator::UploadToGPU(ID3D11Device* device, ID3D11DeviceContext*) {
    const int N = TerrainGenerator::N;

    // Heightmap: R32_FLOAT
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width     = N;
        td.Height    = N;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format    = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc = { 1, 0 };
        td.Usage     = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{ m_heightmap.data(), (UINT)(N * sizeof(float)), 0 };
        m_heightTex.Reset();
        m_heightmapSRV.Reset();
        HR(device->CreateTexture2D(&td, &sr, &m_heightTex));
        HR(device->CreateShaderResourceView(m_heightTex.Get(), nullptr, &m_heightmapSRV));
    }

    // Normal map: R16G16B16A16_FLOAT
    {
        // Convert XMFLOAT4 (32-bit) to 16-bit half floats
        std::vector<uint16_t> half16(N * N * 4);
        for (int i = 0; i < N * N; i++) {
            // Simple float→half conversion using XMHALF
            auto toHalf = [](float f) -> uint16_t {
                return (uint16_t)XMConvertFloatToHalf(f);
            };
            half16[i*4+0] = toHalf(m_normals[i].x);
            half16[i*4+1] = toHalf(m_normals[i].y);
            half16[i*4+2] = toHalf(m_normals[i].z);
            half16[i*4+3] = toHalf(1.0f);
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width     = N;
        td.Height    = N;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc = { 1, 0 };
        td.Usage     = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{ half16.data(), (UINT)(N * 4 * sizeof(uint16_t)), 0 };
        m_normalTex.Reset();
        m_normalSRV.Reset();
        HR(device->CreateTexture2D(&td, &sr, &m_normalTex));
        HR(device->CreateShaderResourceView(m_normalTex.Get(), nullptr, &m_normalSRV));
    }

    // Clear the ready flag so this only runs once per generation
    m_ready.store(false);
    m_running.store(false);

    // --- Morpho attribute maps: RGBA16F each ---
    auto uploadMorpho = [&](const std::vector<XMFLOAT4>& data,
                            ComPtr<ID3D11Texture2D>& texOut,
                            ComPtr<ID3D11ShaderResourceView>& srvOut)
    {
        std::vector<uint16_t> half16(N * N * 4);
        for (int i = 0; i < N * N; ++i) {
            half16[i*4+0] = XMConvertFloatToHalf(data[i].x);
            half16[i*4+1] = XMConvertFloatToHalf(data[i].y);
            half16[i*4+2] = XMConvertFloatToHalf(data[i].z);
            half16[i*4+3] = XMConvertFloatToHalf(data[i].w);
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = N;  td.Height = N;
        td.MipLevels = 1;  td.ArraySize = 1;
        td.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc = { 1, 0 };
        td.Usage     = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{ half16.data(), (UINT)(N * 4 * sizeof(uint16_t)), 0 };
        texOut.Reset();  srvOut.Reset();
        HR(device->CreateTexture2D(&td, &sr, &texOut));
        HR(device->CreateShaderResourceView(texOut.Get(), nullptr, &srvOut));
    };
    if (!m_morpho1.empty()) uploadMorpho(m_morpho1, m_morpho1Tex, m_morpho1SRV);
    if (!m_morpho2.empty()) uploadMorpho(m_morpho2, m_morpho2Tex, m_morpho2SRV);
}

float TerrainGenerator::SampleHeight(float normX, float normZ) const {
    if (m_heightmap.empty()) return 0.0f;
    float px = normX * (N - 1);
    float py = normZ * (N - 1);
    return BilinearHeight(m_heightmap, N, px, py) * TERRAIN_HEIGHT;
}

// ---------------------------------------------------------------------------
//  Florinsky (2009) morphometric attributes via 3x3 Evans/Young window
//  Computes slope G, aspect A, plan curvature kh, profile curvature kv,
//  mean curvature kM, and topographic wetness index TWI via D8 flow accum.
//  All attributes are normalised to [0,1] for RGBA16F GPU upload.
// ---------------------------------------------------------------------------
void TerrainGenerator::ComputeFlorinsky() {
    const int Nd = N;
    const float cellSz = TERRAIN_WORLD_SIZE / (float)(Nd - 1);  // metres per cell

    m_morpho1.resize(Nd * Nd, { 0,0,0.5f,0.5f });
    m_morpho2.resize(Nd * Nd, { 0.5f,0,0.5f,0.5f });

    // Helper: height at cell (cx, cy) in world-space metres
    auto hAt = [&](int cx, int cy) -> float {
        cx = std::clamp(cx, 0, Nd - 1);
        cy = std::clamp(cy, 0, Nd - 1);
        return m_heightmap[cy * Nd + cx] * TERRAIN_HEIGHT;
    };

    struct RawAttr { float slope, aspect, kh, kv, kM; };
    std::vector<RawAttr> raw(Nd * Nd, {});

    float maxSlope = 1e-6f;
    float minKh = 1e9f, maxKh = -1e9f;
    float minKv = 1e9f, maxKv = -1e9f;
    float minKM = 1e9f, maxKM = -1e9f;
    float minExH = 1e9f, maxExH = -1e9f;

    const float inv2dz = 1.0f / (2.0f * cellSz);
    const float invDz2 = 1.0f / (cellSz * cellSz);
    const float inv4dz2 = 1.0f / (4.0f * cellSz * cellSz);
    const float eps = 1e-9f;

    for (int y = 0; y < Nd; ++y) {
        for (int x = 0; x < Nd; ++x) {
            float z00 = hAt(x-1,y-1), z10 = hAt(x,y-1), z20 = hAt(x+1,y-1);
            float z01 = hAt(x-1,y  ),                    z21 = hAt(x+1,y  );
            float z02 = hAt(x-1,y+1), z12 = hAt(x,y+1), z22 = hAt(x+1,y+1);
            float z11 = hAt(x, y);

            // First-order partials (Evans 1972 / Florinsky 2009)
            float p  = (z21 - z01) * inv2dz;               // dz/dx
            float q  = (z12 - z10) * inv2dz;               // dz/dy

            // Second-order partials
            float r  = (z21 - 2.0f*z11 + z01) * invDz2;   // d2z/dx2
            float tc = (z12 - 2.0f*z11 + z10) * invDz2;   // d2z/dy2
            float s  = (z22 - z02 - z20 + z00) * inv4dz2; // d2z/dxdy

            float pq2 = p*p + q*q;
            float G   = sqrtf(pq2);                        // slope gradient (tan(slope))
            float A   = atan2f(q, p);                      // aspect azimuth [-pi,+pi]

            float denom1 = pq2 * powf(pq2 + 1.0f, 1.5f) + eps;
            float denom2 = 2.0f * powf(pq2 + 1.0f, 1.5f) + eps;

            // Plan curvature: >0 = divergent ridge, <0 = convergent hollow
            float kh = -(q*q*r - 2.0f*p*q*s + p*p*tc) / denom1;
            // Profile curvature: >0 = concave-up (decelerating flow)
            float kv = -(p*p*r + 2.0f*p*q*s + q*q*tc) / denom1;
            // Mean curvature
            float kM = -(r*(1.0f + q*q) - 2.0f*p*q*s + tc*(1.0f + p*p)) / denom2;

            raw[y * Nd + x] = { G, A, kh, kv, kM };

            maxSlope = std::max(maxSlope, G);
            minKh = std::min(minKh, kh);  maxKh = std::max(maxKh, kh);
            minKv = std::min(minKv, kv);  maxKv = std::max(maxKv, kv);
            minKM = std::min(minKM, kM);  maxKM = std::max(maxKM, kM);
            float exH = kh - kv;
            minExH = std::min(minExH, exH);  maxExH = std::max(maxExH, exH);
        }
    }

    // --- D8 flow accumulation for TWI ----------------------------------
    std::vector<float> flowAccum(Nd * Nd, 1.0f);

    // Sort cells descending by elevation (process ridges → valleys)
    std::vector<std::pair<float,int>> order;
    order.reserve(Nd * Nd);
    for (int i = 0; i < Nd * Nd; ++i)
        order.push_back({ m_heightmap[i], i });
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b){ return a.first > b.first; });

    // 8-connected neighbour offsets and their distances (diagonal = sqrt(2))
    static const int  dxN[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int  dyN[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
    static const float dd[8] = { 1.41421356f, 1.0f, 1.41421356f, 1.0f,
                                 1.0f, 1.41421356f, 1.0f, 1.41421356f };

    for (auto& [h, idx] : order) {
        int cx = idx % Nd, cy = idx / Nd;
        float cH = m_heightmap[idx];
        float maxDrop = 0.0f;  int best = -1;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + dxN[d], ny = cy + dyN[d];
            if (nx < 0 || nx >= Nd || ny < 0 || ny >= Nd) continue;
            float drop = (cH - m_heightmap[ny * Nd + nx]) / dd[d];
            if (drop > maxDrop) { maxDrop = drop; best = ny * Nd + nx; }
        }
        if (best >= 0) flowAccum[best] += flowAccum[idx];
    }

    // Compute TWI = ln(A * cellArea / tan(slope)) with clamping
    const float cellArea = cellSz * cellSz;
    std::vector<float> twi(Nd * Nd);
    float twiMin = 1e9f, twiMax = -1e9f;
    for (int i = 0; i < Nd * Nd; ++i) {
        float tanSlope = std::max(raw[i].slope, 0.002f);
        float w = logf((flowAccum[i] * cellArea) / tanSlope);
        twi[i] = w;
        twiMin = std::min(twiMin, w);  twiMax = std::max(twiMax, w);
    }

    // --- Normalise and pack into m_morpho1 / m_morpho2 ----------------
    float rkh = std::max(std::abs(minKh), std::abs(maxKh)) * 2.0f + eps;
    float rkv = std::max(std::abs(minKv), std::abs(maxKv)) * 2.0f + eps;
    float rkM = std::max(std::abs(minKM), std::abs(maxKM)) * 2.0f + eps;
    float rExH = std::max(std::abs(minExH), std::abs(maxExH)) * 2.0f + eps;
    float twiRange = std::max(twiMax - twiMin, 1.0f);
    static const float kPi = 3.14159265358979f;

    for (int i = 0; i < Nd * Nd; ++i) {
        const RawAttr& a = raw[i];
        float sN   = std::clamp(a.slope / maxSlope, 0.0f, 1.0f);
        float aspN = (a.aspect + kPi) / (2.0f * kPi);         // [0,1]
        float khN  = std::clamp((a.kh + rkh * 0.5f) / rkh, 0.0f, 1.0f);
        float kvN  = std::clamp((a.kv + rkv * 0.5f) / rkv, 0.0f, 1.0f);
        float kMN  = std::clamp((a.kM + rkM * 0.5f) / rkM, 0.0f, 1.0f);
        float twiN = std::clamp((twi[i] - twiMin) / twiRange, 0.0f, 1.0f);
        float exHN = std::clamp(((a.kh - a.kv) + rExH * 0.5f) / rExH, 0.0f, 1.0f);
        float exVN = kMN;   // vertical excess ~ mean curvature proxy
        m_morpho1[i] = { sN,   aspN, khN, kvN };
        m_morpho2[i] = { kMN,  twiN, exHN, exVN };
    }
}
