#ifndef DYNAMICCPP_EXPORTS
#include "ShaderPSO.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include "Shader.h"

bool ShaderPSO::ReflectConstantBuffers()
{
    m_cbByName.clear();

    bool result{ false };

    auto reflectStage = [&](auto shaderPtr, ShaderStage stage) -> bool
    {
        if (nullptr == shaderPtr) return false;

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
        if (FAILED(D3DReflect(shaderPtr->GetBufferPointer(),
                              shaderPtr->GetBufferSize(),
                              IID_ID3D11ShaderReflection,
                              reinterpret_cast<void**>(reflector.GetAddressOf()))))
        {
            return false;
        }
        ReflectShader(reflector.Get(), stage);
    };

    result = reflectStage(m_vertexShader, ShaderStage::Vertex);
    result = reflectStage(m_pixelShader, ShaderStage::Pixel);
    result = reflectStage(m_geometryShader, ShaderStage::Geometry);
    result = reflectStage(m_hullShader, ShaderStage::Hull);
    result = reflectStage(m_domainShader, ShaderStage::Domain);

    return result;
}

bool ShaderPSO::CreateInputLayoutFromShader()
{
    if (!m_vertexShader)
        return false;

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> refl;
    if (FAILED(D3DReflect(m_vertexShader->GetBufferPointer(),
        m_vertexShader->GetBufferSize(),
        IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(refl.GetAddressOf()))))
    {
        return false;
    }

    D3D11_SHADER_DESC shaderDesc{};
    refl->GetDesc(&shaderDesc);

    InputLayOutContainer layout;
    layout.reserve(shaderDesc.InputParameters);

    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC paramDesc{};
        refl->GetInputParameterDesc(i, &paramDesc);

        D3D11_INPUT_ELEMENT_DESC elem{};
        elem.SemanticName = paramDesc.SemanticName;
        elem.SemanticIndex = paramDesc.SemanticIndex;
        elem.InputSlot = 0;
        elem.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
        elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = 0;

        if (paramDesc.Mask == 1)       elem.Format = DXGI_FORMAT_R32_FLOAT;
        else if (paramDesc.Mask <= 3)  elem.Format = DXGI_FORMAT_R32G32_FLOAT;
        else if (paramDesc.Mask <= 7)  elem.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        else                           elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

        layout.push_back(elem);
    }

    CreateInputLayout(std::move(layout));

    return true;
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

        AddOrMergeCB(cb, cbDesc, stage, bindDesc.BindPoint);
    }
}

void ShaderPSO::AddOrMergeCB(ID3D11ShaderReflectionConstantBuffer* cb, const D3D11_SHADER_BUFFER_DESC& cbDesc, ShaderStage stage, UINT bindPoint)
{
    auto it = m_cbByName.find(cbDesc.Name);
    if (it == m_cbByName.end())
    {
        D3D11_BUFFER_DESC desc{};
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = cbDesc.Size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if (SUCCEEDED(DeviceState::g_pDevice->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())))
        {
            CBEntry entry;
            entry.name = cbDesc.Name;
            entry.size = cbDesc.Size;
            entry.buffer = buffer;
            entry.binds.push_back({ stage, bindPoint });
            entry.cpuData.resize(cbDesc.Size);
            entry.variables.reserve(cbDesc.Variables);

            for (UINT v = 0; v < cbDesc.Variables; ++v)
            {
                ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                D3D11_SHADER_VARIABLE_DESC varDesc{};
                var->GetDesc(&varDesc);
                ID3D11ShaderReflectionType* type = var->GetType();
                D3D11_SHADER_TYPE_DESC typeDesc{};
                type->GetDesc(&typeDesc);
                D3D_SHADER_VARIABLE_CLASS varClass = typeDesc.Class;
                if (typeDesc.Type == D3D_SVT_FLOAT && varDesc.Size == sizeof(float) * 16)
                {
                    varClass = D3D_SVC_MATRIX_ROWS;
                }

                entry.variables.push_back({
                    varDesc.Name ? varDesc.Name : "",
                    varDesc.StartOffset,
                    varDesc.Size,
                    typeDesc.Type,
                    varClass });
            }

            m_cbByName.emplace(entry.name, std::move(entry));
        }
    }
    else
    {
        CBEntry& entry = it->second;
        auto dup = std::find_if(entry.binds.begin(), entry.binds.end(),
            [&](const CBBinding& b) { return b.stage == stage && b.slot == bindPoint; });
        if (dup == entry.binds.end())
            entry.binds.push_back({ stage, bindPoint });
    }
}

void ShaderPSO::Apply()
{
    Apply(DeviceState::g_pDeviceContext);
}

void ShaderPSO::Apply(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;

    PipelineStateObject::Apply(ctx);

    for (auto& kv : m_cbByName)
    {
        CBEntry& cb = kv.second;
        ID3D11Buffer* buf = cb.buffer.Get();
        for (const auto& b : cb.binds)
            SetCBForStage(ctx, b.stage, b.slot, buf);
    }

    for (const auto& sr : m_shaderResources)
    {
        ID3D11ShaderResourceView* view = sr.view.Get();
        SetSRVForStage(ctx, sr.stage, sr.slot, view);
    }

    ResolveSrvUavHazards(ctx);

    for (const auto& ua : m_unorderedAccessViews)
    {
        ID3D11UnorderedAccessView* view = ua.view.Get();
        SetUAVForStage(ctx, ua.stage, ua.slot, view);
    }
}

bool ShaderPSO::UpdateVariable(std::string_view cbName, std::string_view varName, const void* data, size_t size)
{
    auto cbIt = m_cbByName.find(std::string(cbName));
    if (cbIt == m_cbByName.end()) return false;
    CBEntry& cb = cbIt->second;
    auto varIt = std::find_if(cb.variables.begin(), cb.variables.end(),
        [&](const VariableDesc& v) { return v.name == varName; });
    if (varIt == cb.variables.end()) return false;
    if (size > varIt->size) return false;
    std::memcpy(cb.cpuData.data() + varIt->offset, data, size);
    DeviceState::g_pDeviceContext->UpdateSubresource(cb.buffer.Get(), 0, nullptr, cb.cpuData.data(), 0, 0);
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

bool ShaderPSO::UpdateConstantBuffer(ID3D11DeviceContext* ctx, std::string_view name, const void* data, size_t size)
{
    if (!ctx) return false;
    auto it = m_cbByName.find(std::string(name));
    if (it == m_cbByName.end()) return false;
    if (size > it->second.size) return false;

    CBEntry& cb = it->second;
    std::memcpy(cb.cpuData.data(), data, size);
    ctx->UpdateSubresource(cb.buffer.Get(), 0, nullptr, cb.cpuData.data(), 0, 0);
    return true;
}

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
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
            nullptr, nullptr,
            slot, 1, &uav, nullptr);
        break;
    default:
        break;
    }
}

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
                ID3D11ShaderResourceView* nullSRV = nullptr;
                SetSRVForStage(ctx, sr.stage, sr.slot, nullSRV);
            }
        }
    }
}

#endif // !DYNAMICCPP_EXPORTS
