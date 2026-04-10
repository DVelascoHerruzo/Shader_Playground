#pragma once
#include "Types.h"
#include <filesystem>
#include <functional>

// ---------------------------------------------------------------------------
//  ShaderStage flags
// ---------------------------------------------------------------------------
enum class ShaderStage : uint8_t {
    Vertex   = 1 << 0,
    Hull     = 1 << 1,
    Domain   = 1 << 2,
    Pixel    = 1 << 3,
};

// ---------------------------------------------------------------------------
//  ShaderProgram
//  Compiles and owns one set of shaders from a single .hlsl file (multiple
//  entry points), or separate .hlsl files per stage.
// ---------------------------------------------------------------------------
class ShaderProgram {
public:
    // Create from a single file, multiple entry points (pass "" to skip a stage)
    void Create(ID3D11Device* device,
                const wchar_t* vsFile, const char* vsEntry,
                const wchar_t* psFile, const char* psEntry,
                const wchar_t* hsFile = nullptr, const char* hsEntry = nullptr,
                const wchar_t* dsFile = nullptr, const char* dsEntry = nullptr);

    // Add an input layout (call after Create for vertex-based pipelines)
    void CreateInputLayout(ID3D11Device* device,
                           const D3D11_INPUT_ELEMENT_DESC* elems, UINT count);

    // Bind all stages to the pipeline
    void Bind(ID3D11DeviceContext* ctx) const;

    // Hot-reload: recompile if any source file has changed on disk. Returns true if reloaded.
    bool HotReload(ID3D11Device* device);

    // Returns bytecode blob of VS (needed for CreateInputLayout or hull linking)
    ID3DBlob* VSBlob() const { return m_vsBlob.Get(); }

private:
    static ComPtr<ID3DBlob> CompileStage(const wchar_t* file, const char* entry, const char* target);

    struct FileEntry { std::wstring path; std::filesystem::file_time_type mtime; };
    std::vector<FileEntry> m_files;  // files watched for hot-reload

    // Shader stages
    ComPtr<ID3D11VertexShader>   m_vs;
    ComPtr<ID3D11PixelShader>    m_ps;
    ComPtr<ID3D11HullShader>     m_hs;
    ComPtr<ID3D11DomainShader>   m_ds;
    ComPtr<ID3D11InputLayout>    m_inputLayout;
    ComPtr<ID3DBlob>             m_vsBlob;

    // Store creation params for hot-reload
    struct CreationParams {
        std::wstring vsFile, psFile, hsFile, dsFile;
        std::string  vsEntry, psEntry, hsEntry, dsEntry;
    } m_params;
    ID3D11Device* m_devicePtr = nullptr; // weak ref for hot-reload input layout recreation
    D3D11_INPUT_ELEMENT_DESC* m_layoutElems = nullptr;
    UINT m_layoutCount = 0;
    std::vector<D3D11_INPUT_ELEMENT_DESC> m_storedLayout;
};
