#pragma once
#include "PSO.h"
#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

enum class ShaderStage
{
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain
};

class ShaderPSO : public PipelineStateObject
{
public:
    ShaderPSO() = default;
    ~ShaderPSO() = default;

    // Reflect all attached shaders and create constant buffers automatically.
    void ReflectConstantBuffers();

    // Apply pipeline state and bind constant buffers and resources to the GPU.
    void Apply();

    // Bind a shader resource view to a specific shader stage and slot.
    void BindShaderResource(ShaderStage stage, uint32_t slot, ID3D11ShaderResourceView* view);

    // Bind an unordered access view to a specific shader stage and slot.
    void BindUnorderedAccess(ShaderStage stage, uint32_t slot, ID3D11UnorderedAccessView* view);

    // Update a reflected constant buffer by name. Returns false when not found or size mismatch.
    bool UpdateConstantBuffer(std::string_view name, const void* data, size_t size);

    template <typename T>
    bool UpdateConstantBuffer(std::string_view name, const T& data)
    {
        return UpdateConstantBuffer(name, &data, sizeof(T));
    }

	// Get the GUID of the shader PSO.
    const FileGuid& GetShaderPSOGuid() const { return m_shaderPSOGuid; }
    // Set the GUID of the shader PSO.
    void SetShaderPSOGuid(const FileGuid& guid) { m_shaderPSOGuid = guid; }

private:
    struct ConstantBuffer
    {
        std::string name;
        ShaderStage stage;
        uint32_t slot;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        uint32_t size;
    };

    struct ShaderResource
    {
        ShaderStage stage;
        uint32_t slot;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };

    struct UnorderedAccess
    {
        ShaderStage stage;
        uint32_t slot;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
    };

    void ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage);

    std::vector<ConstantBuffer> m_constantBuffers;
    std::vector<ShaderResource> m_shaderResources;
    std::vector<UnorderedAccess> m_unorderedAccessViews;

    FileGuid m_shaderPSOGuid{};
};