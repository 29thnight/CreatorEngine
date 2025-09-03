#pragma once
#include <variant>
#include <DirectXTK/SpriteFont.h>
#include "Core.Minimal.h"

class Texture;
class ImageComponent;
class TextComponent;

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
        Mathf::Vector2                          scale{ 1.f, 1.f };
        float                                   rotation{ 0.f };
        int                                     layerOrder{ 0 };
    };

    struct TextData 
    {
        DirectX::SpriteFont*                    font{ nullptr };
        std::string                             message;
        DirectX::XMVECTORF32                    color{ DirectX::Colors::Black };
        DirectX::XMFLOAT2                       position{};
        float                                   fontSize{ 5.f };
        int                                     layerOrder{ 0 };
    };

    explicit UIRenderProxy(ImageComponent* image) noexcept;
    explicit UIRenderProxy(TextComponent* text) noexcept;
    ~UIRenderProxy();

    void Draw(std::unique_ptr<DirectX::SpriteBatch>& spriteBatch) const;
    void DestroyProxy();

private:
    friend class RenderPassData;
	friend class ProxyCommand;
    std::variant<ImageData, TextData> m_data;
    HashedGuid			              m_instancedID{};
};
