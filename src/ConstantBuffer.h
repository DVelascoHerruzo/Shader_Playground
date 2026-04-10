#pragma once
#include "Types.h"

// ---------------------------------------------------------------------------
//  ConstantBuffer<T>
//  Typed wrapper around a D3D11 constant buffer.
//  Usage:
//    ConstantBuffer<CameraData> cb;
//    cb.Create(device);
//    cb.Update(context, data);
//    cb.VSBind(context, 0);
// ---------------------------------------------------------------------------
template<typename T>
class ConstantBuffer {
public:
    void Create(ID3D11Device* device) {
        static_assert(sizeof(T) % 16 == 0,
            "Constant buffer struct must be 16-byte aligned. Add padding.");
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = static_cast<UINT>(sizeof(T));
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HR(device->CreateBuffer(&bd, nullptr, &m_buffer));
    }

    void Update(ID3D11DeviceContext* ctx, const T& data) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HR(ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        memcpy(mapped.pData, &data, sizeof(T));
        ctx->Unmap(m_buffer.Get(), 0);
    }

    void VSBind(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* buf = m_buffer.Get();
        ctx->VSSetConstantBuffers(slot, 1, &buf);
    }
    void PSBind(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* buf = m_buffer.Get();
        ctx->PSSetConstantBuffers(slot, 1, &buf);
    }
    void HSBind(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* buf = m_buffer.Get();
        ctx->HSSetConstantBuffers(slot, 1, &buf);
    }
    void DSBind(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* buf = m_buffer.Get();
        ctx->DSSetConstantBuffers(slot, 1, &buf);
    }
    void AllBind(ID3D11DeviceContext* ctx, UINT slot) const {
        ID3D11Buffer* buf = m_buffer.Get();
        ctx->VSSetConstantBuffers(slot, 1, &buf);
        ctx->HSSetConstantBuffers(slot, 1, &buf);
        ctx->DSSetConstantBuffers(slot, 1, &buf);
        ctx->PSSetConstantBuffers(slot, 1, &buf);
    }

    ID3D11Buffer* Get() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Buffer> m_buffer;
};
