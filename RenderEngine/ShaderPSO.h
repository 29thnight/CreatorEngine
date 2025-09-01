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

	void Apply(ID3D11DeviceContext* deferredContext);

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
    struct CBBinding {
        ShaderStage stage;
        UINT        slot;
    };

    struct CBEntry {
        std::string name;
        UINT        size = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;   // 이름당 단일 버퍼
        std::vector<CBBinding> binds;                  // (stage, slot) 목록
    };

    struct ShaderResource {
        ShaderStage stage;
        UINT        slot;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };

    struct UnorderedAccess {
        ShaderStage stage;
        UINT        slot;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
    };

    // 리플렉션 세부
    void ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage);
    void AddOrMergeCB(const D3D11_SHADER_BUFFER_DESC& cbDesc, ShaderStage stage, UINT bindPoint);

    // 스테이지별 바인딩 유틸
    static void SetCBForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11Buffer* buf);
    static void SetSRVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11ShaderResourceView* srv);
    static void SetUAVForStage(ID3D11DeviceContext* ctx, ShaderStage st, UINT slot, ID3D11UnorderedAccessView* uav);

    // SRV↔UAV 해저드 정리
    void ResolveSrvUavHazards(ID3D11DeviceContext* ctx);

private:
    // 이름 → 단일 버퍼
    std::unordered_map<std::string, CBEntry> m_cbByName;

    // SRV/UAV 캐시
    std::vector<ShaderResource>  m_shaderResources;
    std::vector<UnorderedAccess> m_unorderedAccessViews;

    FileGuid m_shaderPSOGuid{};
};
#else
class ShaderPSO : public PipelineStateObject
{
public:
    ShaderPSO() = default;
	~ShaderPSO() = default;
};
#endif // !DYNAMICCPP_EXPORTS