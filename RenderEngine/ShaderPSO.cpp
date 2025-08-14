#ifndef DYNAMICCPP_EXPORTS
#include "ShaderPSO.h"
#include <d3dcompiler.h>
#include <algorithm>

void ShaderPSO::ReflectConstantBuffers()
{
    m_constantBuffers.clear();

    auto reflectStage = [&](auto shader, ShaderStage stage)
        {
            if (!shader) return;
            Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
            if (FAILED(D3DReflect(shader->GetBufferPointer(),
                shader->GetBufferSize(),
                IID_ID3D11ShaderReflection,
                reinterpret_cast<void**>(reflector.GetAddressOf()))))
            {
                return;
            }
            ReflectShader(reflector.Get(), stage);
        };

    reflectStage(m_vertexShader, ShaderStage::Vertex);
    reflectStage(m_pixelShader, ShaderStage::Pixel);
    reflectStage(m_geometryShader, ShaderStage::Geometry);
    reflectStage(m_hullShader, ShaderStage::Hull);
    reflectStage(m_domainShader, ShaderStage::Domain);
    reflectStage(m_computeShader, ShaderStage::Compute);
}

void ShaderPSO::ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage)
{
    if (!reflection) return;

    D3D11_SHADER_DESC shaderDesc{};
    reflection->GetDesc(&shaderDesc);

    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        auto new_cbuffer = reflection->GetConstantBufferByIndex(i);

        D3D11_SHADER_BUFFER_DESC cbDesc{};
        new_cbuffer->GetDesc(&cbDesc);

        // cbDesc.Name이 nullptr일 수 있으므로 체크
        if (!cbDesc.Name)
            continue;

        D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
        if (FAILED(reflection->GetResourceBindingDescByName(cbDesc.Name, &bindDesc)))
            continue;

        if (bindDesc.Type != D3D_SIT_CBUFFER)
            continue;

        D3D11_BUFFER_DESC desc{};
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = cbDesc.Size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        if (SUCCEEDED(DeviceState::g_pDevice->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())))
        {
            m_constantBuffers.push_back(ConstantBuffer{
                cbDesc.Name,
                stage,
                bindDesc.BindPoint,
                buffer,
                cbDesc.Size
                });
        }
    }
}

void ShaderPSO::Apply()
{
    PipelineStateObject::Apply();

    for (const auto& cb : m_constantBuffers)
    {
        ID3D11Buffer* buf = cb.buffer.Get();
        switch (cb.stage)
        {
        case ShaderStage::Vertex:
            DeviceState::g_pDeviceContext->VSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        case ShaderStage::Pixel:
            DeviceState::g_pDeviceContext->PSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        case ShaderStage::Geometry:
            DeviceState::g_pDeviceContext->GSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        case ShaderStage::Hull:
            DeviceState::g_pDeviceContext->HSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        case ShaderStage::Domain:
            DeviceState::g_pDeviceContext->DSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        case ShaderStage::Compute:
            DeviceState::g_pDeviceContext->CSSetConstantBuffers(cb.slot, 1, &buf);
            break;
        }
    }
}

bool ShaderPSO::UpdateConstantBuffer(std::string_view name, const void* data, size_t size)
{
    auto it = std::find_if(m_constantBuffers.begin(), m_constantBuffers.end(),
        [&](const ConstantBuffer& cb) { return cb.name == name; });
    if (it == m_constantBuffers.end() || size > it->size)
        return false;

    DeviceState::g_pDeviceContext->UpdateSubresource(it->buffer.Get(), 0, nullptr, data, 0, 0);
    return true;
}

#endif // !DYNAMICCPP_EXPORTS