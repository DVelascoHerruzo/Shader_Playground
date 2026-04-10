#include "Shader.h"
#include <stdio.h>

static bool g_shaderDebug =
#ifdef _DEBUG
    true;
#else
    false;
#endif

ComPtr<ID3DBlob> ShaderProgram::CompileStage(const wchar_t* file, const char* entry, const char* target) {
    if (!file || !entry || entry[0] == '\0') return nullptr;
    if (!std::filesystem::exists(file)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Shader file not found:\n%ls", file);
        MessageBoxA(nullptr, buf, "Shader Error", MB_OK | MB_ICONWARNING);
        return nullptr;
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (g_shaderDebug)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;

    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompileFromFile(file, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, target, flags, 0, &blob, &err);
    if (FAILED(hr)) {
        char buf[2048];
        const char* errMsg = err ? (const char*)err->GetBufferPointer() : "Unknown error";
        snprintf(buf, sizeof(buf), "Shader compile failed: %ls\nEntry: %s\n\n%s", file, entry, errMsg);
        MessageBoxA(nullptr, buf, "Shader Error", MB_OK | MB_ICONERROR);
        return nullptr;
    }
    return blob;
}

void ShaderProgram::Create(ID3D11Device* device,
    const wchar_t* vsFile, const char* vsEntry,
    const wchar_t* psFile, const char* psEntry,
    const wchar_t* hsFile, const char* hsEntry,
    const wchar_t* dsFile, const char* dsEntry)
{
    m_devicePtr = device;
    m_params.vsFile  = vsFile  ? vsFile  : L"";
    m_params.psFile  = psFile  ? psFile  : L"";
    m_params.hsFile  = hsFile  ? hsFile  : L"";
    m_params.dsFile  = dsFile  ? dsFile  : L"";
    m_params.vsEntry = vsEntry ? vsEntry : "";
    m_params.psEntry = psEntry ? psEntry : "";
    m_params.hsEntry = hsEntry ? hsEntry : "";
    m_params.dsEntry = dsEntry ? dsEntry : "";

    m_files.clear();

    auto tryCompile = [&](const std::wstring& f, const std::string& e, const char* tgt) -> ComPtr<ID3DBlob> {
        if (f.empty() || e.empty()) return nullptr;
        auto blob = CompileStage(f.c_str(), e.c_str(), tgt);
        if (!f.empty() && std::filesystem::exists(f))
            m_files.push_back({ f, std::filesystem::last_write_time(f) });
        return blob;
    };

    // VS
    if (auto b = tryCompile(m_params.vsFile, m_params.vsEntry, "vs_5_0")) {
        m_vsBlob = b;
        HR(device->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_vs));
    }
    // PS
    if (auto b = tryCompile(m_params.psFile, m_params.psEntry, "ps_5_0"))
        HR(device->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_ps));
    // HS
    if (auto b = tryCompile(m_params.hsFile, m_params.hsEntry, "hs_5_0"))
        HR(device->CreateHullShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_hs));
    // DS
    if (auto b = tryCompile(m_params.dsFile, m_params.dsEntry, "ds_5_0"))
        HR(device->CreateDomainShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_ds));
}

void ShaderProgram::CreateInputLayout(ID3D11Device* device,
    const D3D11_INPUT_ELEMENT_DESC* elems, UINT count)
{
    assert(m_vsBlob && "Must call Create() before CreateInputLayout()");
    m_storedLayout.assign(elems, elems + count);
    HR(device->CreateInputLayout(elems, count,
        m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), &m_inputLayout));
}

void ShaderProgram::Bind(ID3D11DeviceContext* ctx) const {
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ctx->HSSetShader(m_hs.Get(), nullptr, 0);
    ctx->DSSetShader(m_ds.Get(), nullptr, 0);
    ctx->IASetInputLayout(m_inputLayout.Get());
}

bool ShaderProgram::HotReload(ID3D11Device* device) {
    bool changed = false;
    for (auto& fe : m_files) {
        if (!std::filesystem::exists(fe.path)) continue;
        auto mtime = std::filesystem::last_write_time(fe.path);
        if (mtime != fe.mtime) { changed = true; break; }
    }
    if (!changed) return false;

    // Recompile everything
    m_vs.Reset(); m_ps.Reset(); m_hs.Reset(); m_ds.Reset();
    m_vsBlob.Reset(); m_inputLayout.Reset();
    m_files.clear();

    Create(device,
        m_params.vsFile.empty() ? nullptr : m_params.vsFile.c_str(), m_params.vsEntry.c_str(),
        m_params.psFile.empty() ? nullptr : m_params.psFile.c_str(), m_params.psEntry.c_str(),
        m_params.hsFile.empty() ? nullptr : m_params.hsFile.c_str(), m_params.hsEntry.c_str(),
        m_params.dsFile.empty() ? nullptr : m_params.dsFile.c_str(), m_params.dsEntry.c_str());

    if (!m_storedLayout.empty())
        CreateInputLayout(device, m_storedLayout.data(), (UINT)m_storedLayout.size());

    return true;
}
