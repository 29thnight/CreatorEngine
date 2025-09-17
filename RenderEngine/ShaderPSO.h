#pragma once
#include "PSO.h"
#ifndef DYNAMICCPP_EXPORTS
#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <span>
#include <cstddef>

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
    struct CBBinding
    {
        ShaderStage stage;
        UINT        slot;
    };

    struct VariableDesc
    {
        std::string              name;
        UINT                     offset{};
        UINT                     size{};
        D3D_SHADER_VARIABLE_TYPE type{ D3D_SVT_VOID };
        D3D_SHADER_VARIABLE_CLASS varClass{ D3D_SVC_SCALAR };
    };

    struct CBEntry
    {
        std::string name;
        UINT        size = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        std::vector<CBBinding>  binds;
        std::vector<VariableDesc> variables;
        std::vector<std::byte> cpuData;
    };

private:
    struct ShaderResource
    {
        ShaderStage stage;
        UINT        slot;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };

    struct UnorderedAccess
    {
        ShaderStage stage;
        UINT        slot;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
    };

public:
    ShaderPSO();
    ~ShaderPSO() = default;
    ShaderPSO(const ShaderPSO& other);

    // Reflect all attached shaders and create constant buffers automatically.
    void ReflectConstantBuffers();

    void CreateInputLayoutFromShader();

    // Apply pipeline state and bind constant buffers and resources to the GPU.
    void Apply();

    void Apply(ID3D11DeviceContext* deferredContext);

    // Bind a shader resource view to a specific shader stage and slot.
    void BindShaderResource(ShaderStage stage, uint32_t slot, ID3D11ShaderResourceView* view);

    // Bind an unordered access view to a specific shader stage and slot.
    void BindUnorderedAccess(ShaderStage stage, uint32_t slot, ID3D11UnorderedAccessView* view);

    // Update a reflected constant buffer by name. Returns false when not found or size mismatch.
    bool UpdateConstantBuffer(ID3D11DeviceContext* ctx, std::string_view name, const void* data, size_t size);

    template <typename T>
    bool UpdateConstantBuffer(ID3D11DeviceContext* ctx, std::string_view name, const T& data)
    {
        return UpdateConstantBuffer(ctx, name, &data, sizeof(T));
    }

    // Update a single variable inside a constant buffer.
    bool UpdateVariable(std::string_view cbName, std::string_view varName, const void* data, size_t size);

    template <typename T>
    bool UpdateVariable(std::string_view cbName, std::string_view varName, const T& data)
    {
        return UpdateVariable(cbName, varName, &data, sizeof(T));
    }

    // Get the GUID of the shader PSO.
    const FileGuid& GetShaderPSOGuid() const { return m_shaderPSOGuid; }
    // Set the GUID of the shader PSO.
    void SetShaderPSOGuid(const FileGuid& guid) { m_shaderPSOGuid = guid; }

    // Expose reflected constant buffer entries.
    const std::unordered_map<std::string, CBEntry>& GetConstantBuffers() const { return m_cbByName; }
	std::string m_shaderPSOName{ "UnnamedShaderPSO" };

	bool IsInvalidated() const { return m_isInvalidated; }
	void SetInvalidated(bool val) { m_isInvalidated = val; }

private:
    // Reflection helpers
    void ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage);
    void AddOrMergeCB(ID3D11ShaderReflectionConstantBuffer* cb, const D3D11_SHADER_BUFFER_DESC& cbDesc, ShaderStage stage, UINT bindPoint);

    // Internal helpers for binding
    static void SetCBForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11Buffer* buf);
    static void SetSRVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11ShaderResourceView* srv);
    static void SetUAVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11UnorderedAccessView* uav);

    // Resolve SRV/UAV hazards
    void ResolveSrvUavHazards(ID3D11DeviceContext* ctx);

private:
    std::unordered_map<std::string, CBEntry> m_cbByName;

    std::vector<ShaderResource>  m_shaderResources;
    std::vector<UnorderedAccess> m_unorderedAccessViews;

	FileGuid m_shaderPSOGuid{}; //<-- File GUID of the shader PSO
	bool     m_isInvalidated{ false }; //<-- Set to true when shaders are reloaded and PSO needs to be reapplied
};
#else
class ShaderPSO : public PipelineStateObject
{
public:
    ShaderPSO() = default;
    ~ShaderPSO() = default;

    struct VariableDesc {};
    struct CBEntry {};
};
#endif // !DYNAMICCPP_EXPORTS
