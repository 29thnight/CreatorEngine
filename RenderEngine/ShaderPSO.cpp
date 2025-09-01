#ifndef DYNAMICCPP_EXPORTS
#include "ShaderPSO.h"
#include <d3dcompiler.h>
#include <algorithm>

void ShaderPSO::ReflectConstantBuffers()
{
    m_cbByName.clear();

    auto reflectStage = [&](auto shaderPtr, ShaderStage stage)
        {
            if (!shaderPtr) return; // ���̴��� ������ ��ŵ

            Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
            if (FAILED(D3DReflect(shaderPtr->GetBufferPointer(),
                shaderPtr->GetBufferSize(),
                IID_ID3D11ShaderReflection,
                reinterpret_cast<void**>(reflector.GetAddressOf()))))
            {
                return; // ���÷��� ���� �� ��ŵ
            }
            ReflectShader(reflector.Get(), stage);
        };

    reflectStage(m_vertexShader, ShaderStage::Vertex);
    reflectStage(m_pixelShader, ShaderStage::Pixel);
    reflectStage(m_geometryShader, ShaderStage::Geometry);
    reflectStage(m_hullShader, ShaderStage::Hull);
    reflectStage(m_domainShader, ShaderStage::Domain);
}

void ShaderPSO::ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage)
{
    if (!reflection) return;

    D3D11_SHADER_DESC shaderDesc{};
    reflection->GetDesc(&shaderDesc);

    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);

        D3D11_SHADER_BUFFER_DESC cbDesc{};
        cb->GetDesc(&cbDesc);
        if (!cbDesc.Name) continue;

        D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
        if (FAILED(reflection->GetResourceBindingDescByName(cbDesc.Name, &bindDesc)))
            continue;

        if (bindDesc.Type != D3D_SIT_CBUFFER)
            continue;

        AddOrMergeCB(cbDesc, stage, bindDesc.BindPoint);
    }
}

void ShaderPSO::AddOrMergeCB(const D3D11_SHADER_BUFFER_DESC& cbDesc, ShaderStage stage, UINT bindPoint)
{
    auto it = m_cbByName.find(cbDesc.Name);
    if (it == m_cbByName.end())
    {
        // �̸��� ���� 1�� ����
        D3D11_BUFFER_DESC desc{};
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = cbDesc.Size;          // HLSL �����Ϸ��� 16B ���� ����
        desc.Usage = D3D11_USAGE_DEFAULT;  // ��� ������Ʈ�� DYNAMIC + Map/Unmap �ɼ� ���
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if (SUCCEEDED(DeviceState::g_pDevice->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())))
        {
            CBEntry entry;
            entry.name = cbDesc.Name;
            entry.size = cbDesc.Size;
            entry.buffer = buffer;
            entry.binds.push_back({ stage, bindPoint });

            m_cbByName.emplace(entry.name, std::move(entry));
        }
    }
    else
    {
        CBEntry& entry = it->second;

        // ������ ����ġ �� ���ܿ� �α�/��� ���
        // (���⼭�� ���ε��� �߰�)
        auto dup = std::find_if(entry.binds.begin(), entry.binds.end(),
            [&](const CBBinding& b) { return b.stage == stage && b.slot == bindPoint; });
        if (dup == entry.binds.end())
            entry.binds.push_back({ stage, bindPoint });
    }
}

// ---- Apply: ��� ���ؽ�Ʈ ----
void ShaderPSO::Apply()
{
    Apply(DeviceState::g_pDeviceContext);
}

// ---- Apply(ctx): ���� ���ؽ�Ʈ(���۵� ����) ----
void ShaderPSO::Apply(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;

    // ���� ����������/���̴�/���÷�/������Ʈ ���ε�(���� PSO ����)
    PipelineStateObject::Apply(ctx);

    // 1) Constant Buffers: �̸��� 1�� ���۸� ��� (stage,slot)�� ���� ���ε�
    for (auto& kv : m_cbByName)
    {
        CBEntry& cb = kv.second;
        ID3D11Buffer* buf = cb.buffer.Get();
        for (const auto& b : cb.binds)
            SetCBForStage(ctx, b.stage, b.slot, buf);
    }

    // 2) SRVs
    for (const auto& sr : m_shaderResources)
    {
        ID3D11ShaderResourceView* view = sr.view.Get();
        SetSRVForStage(ctx, sr.stage, sr.slot, view);
    }

    // 3) SRV��UAV ������ ����(���� ���ҽ��� SRV/UAV ���� ���ε����� �ʵ���)
    ResolveSrvUavHazards(ctx);

    // 4) UAVs
    for (const auto& ua : m_unorderedAccessViews)
    {
        ID3D11UnorderedAccessView* view = ua.view.Get();
        SetUAVForStage(ctx, ua.stage, ua.slot, view);
    }
}

bool ShaderPSO::UpdateConstantBuffer(std::string_view name, const void* data, size_t size)
{
    auto it = m_cbByName.find(std::string(name));
    if (it == m_cbByName.end()) return false;
    if (size > it->second.size)  return false;

    // ��� ���ؽ�Ʈ ������Ʈ (���� ������ ���� ��å)
    DeviceState::g_pDeviceContext->UpdateSubresource(it->second.buffer.Get(), 0, nullptr, data, 0, 0);
    return true;
}

void ShaderPSO::BindShaderResource(ShaderStage stage, uint32_t slot, ID3D11ShaderResourceView* view)
{
    auto it = std::find_if(m_shaderResources.begin(), m_shaderResources.end(),
        [&](const ShaderResource& sr) { return sr.stage == stage && sr.slot == slot; });
    if (it != m_shaderResources.end())
        it->view = view;
    else
        m_shaderResources.push_back(ShaderResource{ stage, slot, view });
}

void ShaderPSO::BindUnorderedAccess(ShaderStage stage, uint32_t slot, ID3D11UnorderedAccessView* view)
{
    auto it = std::find_if(m_unorderedAccessViews.begin(), m_unorderedAccessViews.end(),
        [&](const UnorderedAccess& u) { return u.stage == stage && u.slot == slot; });
    if (it != m_unorderedAccessViews.end())
        it->view = view;
    else
        m_unorderedAccessViews.push_back(UnorderedAccess{ stage, slot, view });
}

// ---- ���������� ���ε� ��ƿ ----
void ShaderPSO::SetCBForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11Buffer* buf)
{
    switch (st) {
    case ShaderStage::Vertex:   ctx->VSSetConstantBuffers(slot, 1, &buf); break;
    case ShaderStage::Pixel:    ctx->PSSetConstantBuffers(slot, 1, &buf); break;
    case ShaderStage::Geometry: ctx->GSSetConstantBuffers(slot, 1, &buf); break;
    case ShaderStage::Hull:     ctx->HSSetConstantBuffers(slot, 1, &buf); break;
    case ShaderStage::Domain:   ctx->DSSetConstantBuffers(slot, 1, &buf); break;
    }
}

void ShaderPSO::SetSRVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11ShaderResourceView* srv)
{
    switch (st) {
    case ShaderStage::Vertex:   ctx->VSSetShaderResources(slot, 1, &srv); break;
    case ShaderStage::Pixel:    ctx->PSSetShaderResources(slot, 1, &srv); break;
    case ShaderStage::Geometry: ctx->GSSetShaderResources(slot, 1, &srv); break;
    case ShaderStage::Hull:     ctx->HSSetShaderResources(slot, 1, &srv); break;
    case ShaderStage::Domain:   ctx->DSSetShaderResources(slot, 1, &srv); break;
    }
}

void ShaderPSO::SetUAVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11UnorderedAccessView* uav)
{
    switch (st) {
    case ShaderStage::Pixel:
        // ���� RT/DS ����, UAV�� ����(���� �ܰ� ȣ��)
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
            nullptr, nullptr,
            slot, 1, &uav, nullptr);
        break;
    default:
        // V/G/H/D ���������� UAV ������
        break;
    }
}

// ---- SRV��UAV ������ ���� ----
void ShaderPSO::ResolveSrvUavHazards(ID3D11DeviceContext* ctx)
{
    for (const auto& ua : m_unorderedAccessViews)
    {
        if (!ua.view) continue;

        Microsoft::WRL::ComPtr<ID3D11Resource> uavRes;
        ua.view->GetResource(&uavRes);
        if (!uavRes) continue;

        for (const auto& sr : m_shaderResources)
        {
            if (!sr.view) continue;

            Microsoft::WRL::ComPtr<ID3D11Resource> srvRes;
            sr.view->GetResource(&srvRes);

            if (srvRes.Get() == uavRes.Get())
            {
                // ���� ���ҽ��� SRV�ε� ���ε� ���̶�� null ó��
                ID3D11ShaderResourceView* nullSRV = nullptr;
                SetSRVForStage(ctx, sr.stage, sr.slot, nullSRV);
            }
        }
    }
}

#endif // !DYNAMICCPP_EXPORTS