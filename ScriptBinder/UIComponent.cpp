#include "UIComponent.h"
#include "GameObject.h"
#include "UIManager.h"
#include "ShaderSystem.h"

float MaxOreder = 100.0f;

UIComponent::UIComponent()
{
    m_name = "UIComponent"; 
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<UIComponent>();
}

void UIComponent::SetCanvas(Canvas* canvas)
{
   ownerCanvas = canvas;
}

void UIComponent::SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI)
{
    navigation[(int)dir] = otherUI;
    Navigation nav;
    nav.mode = (int)dir;
    nav.navObject = otherUI->GetInstanceID();

    auto it =  std::ranges::find_if(navigations, [&](const Navigation& n)
    {
        return n.mode == nav.mode; 
    });

    if (it == navigations.end())
    {
        navigations.push_back(nav);
    }
    else
    {
	    *it = nav;
    }
}

void UIComponent::DeserializeNavi()
{
    for (const auto& nav : navigations)
    {
        if (auto obj = GameObject::FindInstanceID(nav.navObject))
        {
            navigation[(int)nav.mode] = obj->shared_from_this();
        }
	}
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
    if (auto next = navigation[(int)dir].lock())
		return next.get();

	return nullptr;
}

bool UIComponent::IsNavigationThis()
{
	GameObject* thisObj = GetOwner();
	auto selectedObj = UIManagers->SelectUI.lock();

    if (selectedObj && thisObj == selectedObj.get())
    {
        return true;
    }

    return false;
}

void UIComponent::DeserializeShader()
{
    if (!m_customPixelShaderPath.empty())
    {
        SetCustomPixelShader(m_customPixelShaderPath);
	}
}

void UIComponent::SetCustomPixelShader(std::string_view shaderPath)
{
	m_customPixelShaderPath = shaderPath.data();

    if (shaderPath.empty()) return;

    auto shader = ShaderSystem->PixelShaders[shaderPath.data()];
	std::string cbufferName = "UIBuffer";
	uint32 slot = 1;
    if (!shader.GetShader() || !shader.IsCompiled()) return;

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> refl;
    D3DReflect(shader.GetBufferPointer(), shader.GetBufferSize(), IID_PPV_ARGS(refl.GetAddressOf()));

    D3D11_SHADER_DESC shaderDesc;
    refl->GetDesc(&shaderDesc);

    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        auto cb = refl->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cbDesc;
        cb->GetDesc(&cbDesc);
        if (std::string_view{ cbDesc.Name } != cbufferName) continue;

        for (UINT r = 0; r < shaderDesc.BoundResources; ++r)
        {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
            refl->GetResourceBindingDesc(r, &bindDesc);
            if (bindDesc.Type == D3D_SIT_CBUFFER &&
                std::string_view{ bindDesc.Name } == cbufferName &&
                bindDesc.BindPoint == slot)
            {
                m_customPixelCPUBuffer.resize(cbDesc.Size);
                std::span<std::byte> bufferSpan{ m_customPixelCPUBuffer };
                std::fill(bufferSpan.begin(), bufferSpan.end(), std::byte{ 0 });
                for (UINT v = 0; v < cbDesc.Variables; ++v)
                {
                    auto var = cb->GetVariableByIndex(v);
                    D3D11_SHADER_VARIABLE_DESC vDesc;
                    var->GetDesc(&vDesc);
                    m_variables.emplace(vDesc.Name, VarInfo{ vDesc.StartOffset, vDesc.Size });
                    if (const void* defaultValue = vDesc.DefaultValue)
                    {
                        std::span<std::byte> dest = bufferSpan.subspan(vDesc.StartOffset, vDesc.Size);
                        std::memcpy(dest.data(), defaultValue, vDesc.Size);
                    }
                }
                return;
            }
        }
    }
}

std::optional<float> UIComponent::GetFloat(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            float value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(float));
            return value;
        }
	}
	return std::nullopt;
}

void UIComponent::SetFloat(std::string_view name, float value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(float));
        }
	}
}

std::optional<float2> UIComponent::GetFloat2(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float2) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            float2 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(float2));
            return value;
        }
    }
	return std::nullopt;
}

void UIComponent::SetFloat2(std::string_view name, const float2& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float2) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(float2));
        }
	}
}

std::optional<float3> UIComponent::GetFloat3(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float3) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            float3 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(float3));
            return value;
        }
	}

	return std::nullopt;
}

void UIComponent::SetFloat3(std::string_view name, const float3& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float3) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(float3));
        }
	}
}

std::optional<float4> UIComponent::GetFloat4(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float4) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            float4 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(float4));
            return value;
        }
	}

	return std::nullopt;
}

void UIComponent::SetFloat4(std::string_view name, const float4& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(float4) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(float4));
        }
	}
}

std::optional<int> UIComponent::GetInt(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            int value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(int));
            return value;
        }
    }

	return std::nullopt;
}

void UIComponent::SetInt(std::string_view name, int value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(int));
        }
	}
}

std::optional<int2> UIComponent::GetInt2(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int2) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            int2 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(int2));
            return value;
        }
	}

	return std::nullopt;
}

void UIComponent::SetInt2(std::string_view name, const int2& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int2) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(int2));
        }
	}
}

std::optional<int3> UIComponent::GetInt3(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int3) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            int3 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(int3));
            return value;
        }
	}

	return std::nullopt;
}

void UIComponent::SetInt3(std::string_view name, const int3& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int3) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(int3));
        }
	}
}

std::optional<int4> UIComponent::GetInt4(std::string_view name) const
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int4) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            int4 value;
            std::memcpy(&value, m_customPixelCPUBuffer.data() + var.offset, sizeof(int4));
            return value;
        }
	}

	return std::nullopt;
}

void UIComponent::SetInt4(std::string_view name, const int4& value)
{
    if (auto it = m_variables.find(name.data()); it != m_variables.end())
    {
        const VarInfo& var = it->second;
        if (var.size == sizeof(int4) && m_customPixelCPUBuffer.size() >= var.offset + var.size)
        {
            std::memcpy(m_customPixelCPUBuffer.data() + var.offset, &value, sizeof(int4));
        }
	}
}
