#ifndef DYNAMICCPP_EXPORTS
#include "ShaderPSO.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include "Shader.h"
#include "ShaderSystem.h"

ShaderPSO::ShaderPSO() : PipelineStateObject(true)
{
}

ShaderPSO::ShaderPSO(const ShaderPSO& other) : PipelineStateObject(true)
{
}

void ShaderPSO::ReflectConstantBuffers()
{
    m_cbByName.clear();

    auto reflectStage = [&](auto shaderPtr, ShaderStage stage)
    {
        if (nullptr == shaderPtr) return;

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
        if (FAILED(D3DReflect(shaderPtr->GetBufferPointer(),
                              shaderPtr->GetBufferSize(),
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
}

void ShaderPSO::CreateInputLayoutFromShader()
{
    if (!m_vertexShader)
        return;

    if (!m_inputLayoutDescContainer.empty())
    {
        m_inputLayoutDescContainer.clear();
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> refl;
    if (FAILED(D3DReflect(m_vertexShader->GetBufferPointer(),
        m_vertexShader->GetBufferSize(),
        IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(refl.GetAddressOf()))))
    {
        return;
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

    m_inputLayoutDescContainer = std::move(layout);

    // ★ 여기서 래스터라이저·깊이 상태와 샘플러 둘을 만들던 것을 걷었다 (M1).
    //   이름은 "입력 레이아웃을 만든다"인데 실제로는 DX11 파이프라인 상태를
    //   함께 세우고 있었고, 그것을 거는 Apply의 호출자가 0이었다.
    //   DX12에서 그 상태는 PSO에 구워지므로 자산이 들 것이 아니다.
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
        // ★ 예전에는 여기서 ID3D11Buffer를 만들고, 그 성공 여부로 아래 레이아웃
        //   등록 전체를 감쌌다 (M1에서 걷었다). 즉 리플렉션이 읽어 낸 자료가
        //   DX11 디바이스의 형편에 달려 있었다 - 디바이스가 없으면 레이아웃도
        //   없었다. 지금은 blob만 있으면 세워진다.
        {
            CBEntry entry;
            entry.name = cbDesc.Name;
            entry.size = cbDesc.Size;
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

bool ShaderPSO::UpdateVariable(std::string_view cbName, std::string_view varName, const void* data, size_t size)
{
    auto cbIt = m_cbByName.find(std::string(cbName));
    if (cbIt == m_cbByName.end()) return false;
    CBEntry& cb = cbIt->second;
    auto varIt = std::find_if(cb.variables.begin(), cb.variables.end(),
        [&](const VariableDesc& v) { return v.name == varName; });
    if (varIt == cb.variables.end()) return false;
    if (size > varIt->size) return false;
    if (cb.cpuData.size() != cb.size) cb.cpuData.resize(cb.size);
    std::memcpy(cb.cpuData.data() + varIt->offset, data, size);
    // GPU 반영은 여기서 하지 않는다 - 그리는 쪽(M3)이 프레임 상수 링에 올린다.
    return true;
}

bool ShaderPSO::UpdateConstantBuffer(std::string_view name, const void* data, size_t size)
{
    auto it = m_cbByName.find(std::string(name));
    if (it == m_cbByName.end()) return false;

    CBEntry& entry = it->second;
    if (size != entry.size) return false;

    if (entry.cpuData.size() != entry.size) entry.cpuData.resize(entry.size);
    std::memcpy(entry.cpuData.data(), data, size);
    return true;
}

#endif // !DYNAMICCPP_EXPORTS
