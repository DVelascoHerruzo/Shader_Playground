#pragma once
#include "Types.h"
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

// ---------------------------------------------------------------------------
//  TerrainGenerator
//  Generates a heightmap using domain-warped FBM (FastNoiseLite) then
//  applies Lague-style hydraulic erosion on a background thread.
//  Call Generate() to kick off (async). Poll IsReady() to check completion.
// ---------------------------------------------------------------------------
class TerrainGenerator {
public:
    static constexpr int N = HEIGHTMAP_SIZE;    // 513 x 513 samples

    struct Params {
        int   seed           = 42;
        int   octaves        = 6;
        float frequency      = 0.0025f;
        float lacunarity     = 2.0f;
        float gain           = 0.5f;
        float domainWarpAmp  = 80.0f;
        float detailAmplitude  = 0.018f;   // high-freq detail layer amplitude
        float detailFreqScale  = 10.0f;    // multiplier on base frequency
        int   erosionIters   = 35000;
        int   simScale       = 1;       // generate at N*simScale, downsample to N
        float erosionRadius  = 3.0f;
        float inertia        = 0.3f;
        float sedimentCap    = 4.0f;
        float erosionSpeed   = 0.3f;
        float depositSpeed   = 0.3f;
        float evaporation    = 0.01f;
        float gravity        = 4.0f;
    };

    ~TerrainGenerator();

    // Kick off async generation + erosion
    void Generate(const Params& p, std::function<void()> onComplete = nullptr);

    // Poll from main thread
    bool     IsReady()    const { return m_ready.load(); }
    float    Progress()   const { return m_progress.load(); }
    bool     IsRunning()  const { return m_running.load(); }
    bool     HasTerrain() const { return m_heightmapSRV != nullptr; }

    // Upload results to GPU (call from main thread AFTER IsReady())
    void UploadToGPU(ID3D11Device* device, ID3D11DeviceContext* ctx);

    // Raw CPU data (valid after IsReady())
    const std::vector<float>& GetHeightmap() const { return m_heightmap; }
    float SampleHeight(float normX, float normZ) const;   // normX, normZ in [0,1]

    // GPU resources
    ID3D11ShaderResourceView* HeightmapSRV() const { return m_heightmapSRV.Get(); }
    ID3D11ShaderResourceView* NormalSRV()    const { return m_normalSRV.Get(); }
    // Florinsky morphometric attribute maps
    // morpho1: (slope_norm, aspect_norm, kh_norm, kv_norm)
    // morpho2: (kM_norm,    TWI_norm,    exH_norm, exV_norm)
    ID3D11ShaderResourceView* MorphoSRV1()   const { return m_morpho1SRV.Get(); }
    ID3D11ShaderResourceView* MorphoSRV2()   const { return m_morpho2SRV.Get(); }

    Params params;

private:
    void RunOnThread(Params p, std::function<void()> cb);
    void BuildNormals();
    void ComputeFlorinsky();            // computes m_morpho1, m_morpho2 from m_heightmap
    static float BilinearHeight(const std::vector<float>& h, int N, float x, float y);
    static void  BilinearDeposit(std::vector<float>& h, int N, float x, float y, float amount);

    std::vector<float>         m_heightmap;      // N*N floats, 0..1  (render resolution)
    std::vector<float>         m_simHeightmap;   // (N*simScale)^2 floats — high-res sim buffer
    std::vector<XMFLOAT4>     m_normals;         // N*N, xyz = normal, w = 1
    // Florinsky morphometric attributes (computed CPU-side from heightmap)
    std::vector<XMFLOAT4>     m_morpho1;         // slope_norm, aspect_norm, kh_norm, kv_norm
    std::vector<XMFLOAT4>     m_morpho2;         // kM_norm, TWI_norm, exH_norm, exV_norm

    std::atomic<bool>  m_ready         { false };
    std::atomic<bool>  m_running       { false };
    std::atomic<float> m_progress      { 0.0f };
    std::atomic<bool>  m_stopRequested { false };
    std::thread        m_thread;

    ComPtr<ID3D11Texture2D>          m_heightTex;
    ComPtr<ID3D11ShaderResourceView> m_heightmapSRV;
    ComPtr<ID3D11Texture2D>          m_normalTex;
    ComPtr<ID3D11ShaderResourceView> m_normalSRV;
    // Florinsky GPU textures
    ComPtr<ID3D11Texture2D>          m_morpho1Tex;
    ComPtr<ID3D11ShaderResourceView> m_morpho1SRV;
    ComPtr<ID3D11Texture2D>          m_morpho2Tex;
    ComPtr<ID3D11ShaderResourceView> m_morpho2SRV;
};
