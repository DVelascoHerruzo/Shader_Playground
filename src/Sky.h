#pragma once
#include "Types.h"
#include "Shader.h"

// ---------------------------------------------------------------------------
//  Sky
//  Equirectangular skybox sampled from a panoramic texture.
//  Sun disc + glow overlaid procedurally on top.
//  Rendered first (depth write OFF), using a no-VB VS trick.
// ---------------------------------------------------------------------------
class Sky {
public:
    void Create(ID3D11Device* device);
    void Draw(ID3D11DeviceContext* ctx) const;
    void HotReloadShaders(ID3D11Device* device) { m_shader.HotReload(device); }

    // Called by Renderer after loading the sky panoramas
    void SetSkybox     (ID3D11ShaderResourceView* srv) { m_skyboxSRV      = srv; }
    void SetNightSkybox(ID3D11ShaderResourceView* srv) { m_nightSkyboxSRV = srv; }

private:
    ShaderProgram m_shader;
    ID3D11ShaderResourceView* m_skyboxSRV      = nullptr;  // day HDRI (not owned)
    ID3D11ShaderResourceView* m_nightSkyboxSRV = nullptr;  // night HDRI (not owned)

    ComPtr<ID3D11DepthStencilState> m_dssNoDepthWrite;
    ComPtr<ID3D11RasterizerState>   m_rsCullNone;
};
