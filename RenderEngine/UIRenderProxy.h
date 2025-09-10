#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <variant>
#include <DirectXTK/SpriteFont.h>
#include <cstdint>
#include "Core.Minimal.h"
#include "Shader.h"
#include "Navigation.h"

class Texture;
class ImageComponent;
class TextComponent;
enum class ClipDirection : std::uint8_t;
// Proxy responsible for drawing UI elements without keeping component pointers.
class UIRenderProxy 
{
public:
    struct ImageData 
    {
		std::vector< std::shared_ptr<Texture>>  textures;
        std::shared_ptr<Texture>                texture{ nullptr };
        DirectX::XMFLOAT2                       origin{};
        Mathf::Vector3                          position{};
        Mathf::Color4                           color{ 1.f, 1.f, 1.f, 1.f };
        Mathf::Vector2                          scale{ 1.f, 1.f };
        float                                   rotation{ 0.f };
        int                                     layerOrder{ 0 };
        ClipDirection                           clipDirection{ ClipDirection::None };
        float                                   clipPercent{ 1.f };
    };

    struct TextData 
    {
        DirectX::SpriteFont*                    font{ nullptr };
        std::string                             message;
        Mathf::Color4                           color{ DirectX::Colors::Black };
        DirectX::XMFLOAT2                       position{};
        float                                   fontSize{ 5.f };
        int                                     layerOrder{ 0 };
    };

    explicit UIRenderProxy(ImageComponent* image) noexcept;
    explicit UIRenderProxy(TextComponent* text) noexcept;
    ~UIRenderProxy();

    void Draw(std::unique_ptr<DirectX::SpriteBatch>& spriteBatch) const;
    void DestroyProxy();

	void SetCustomPixelShader(std::string_view shaderPath);
	ShaderPtr<PixelShader> GetCustomPixelShader() const { return m_customPixelShader; }

	void SetCustomPixelBuffer(const std::vector<std::byte>& cpuBuffer);
	ComPtr<ID3D11Buffer> GetCustomPixelBuffer() const { return m_customPixelBuffer; }

	void UpdateShaderBuffer(ID3D11DeviceContext* deferredContext);

	bool isCustomShader() const { return m_customPixelShader != nullptr && m_customPixelBuffer != nullptr; }

    const HashedGuid& GetInstanceID() const { return m_instancedID; }
    bool IsEnabled() const { return m_isEnabled; }
	void SetEnabled(bool enable) { m_isEnabled = enable; }

private:
    friend class RenderPassData;
	friend class ProxyCommand;
    std::variant<ImageData, TextData>   m_data;
    HashedGuid			                m_instancedID{};
    ShaderPtr<PixelShader>              m_customPixelShader{};
	ComPtr<ID3D11Buffer>                m_customPixelBuffer{ nullptr };
	std::vector<std::byte>              m_customPixelCPUBuffer{};
    uint32                              m_customPixelBufferSize{};
	bool                                m_isEnabled{ true };
};
#endif // !DYNAMICCPP_EXPORTS