#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "PSO.h"
#include <d3d11shader.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

class ShaderPSO : public PipelineStateObject
{
public:
    ShaderPSO() = default;
    ~ShaderPSO() = default;

    // Reflect all attached shaders and create constant buffers automatically.
    void ReflectConstantBuffers();

    // Apply pipeline state and bind constant buffers to the GPU.
    void Apply();

    // Update a reflected constant buffer by name. Returns false when not found or size mismatch.
    bool UpdateConstantBuffer(std::string_view name, const void* data, size_t size);

    template <typename T>
    bool UpdateConstantBuffer(std::string_view name, const T& data)
    {
        return UpdateConstantBuffer(name, &data, sizeof(T));
    }

private:
    enum class ShaderStage
    {
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute
    };

    struct ConstantBuffer
    {
        std::string name;
        ShaderStage stage;
        uint32_t slot;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        uint32_t size;
    };

    void ReflectShader(ID3D11ShaderReflection* reflection, ShaderStage stage);

    std::vector<ConstantBuffer> m_constantBuffers;
};

#endif // !DYNAMICCPP_EXPORTS
