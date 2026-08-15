#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <variant>
#include <cstdint>
#include "Core.Minimal.h"
#include "Navigation.h"
#include "SpriteSheet.h"

class Texture;
class ImageComponent;
class TextComponent;
class SpriteSheetComponent;
enum class ClipDirection : std::uint8_t;
enum class TextAlignment : std::uint8_t;
// Proxy responsible for drawing UI elements without keeping component pointers.
class UIRenderProxy : public std::enable_shared_from_this<UIRenderProxy>
{
public:
    struct ImageData 
    {
		std::vector<std::shared_ptr<Texture>>   textures;
        std::shared_ptr<Texture>                texture{ nullptr };
        DirectX::XMFLOAT2                       origin{};
        Mathf::Vector3                          position{};
        float                                   rotation{ 0.f };
        Mathf::Color4                           color{ 1.f, 1.f, 1.f, 1.f };
        Mathf::Vector2                          scale{ 1.f, 1.f };
        int                                     canvasOrder{ 0 };
        int                                     layerOrder{ 0 };
        float                                   clipPercent{ 1.f };
        SpriteEffects                           filpEffect{ SpriteEffects_None };
        ClipDirection                           clipDirection{ ClipDirection::None };
    };

    struct TextData
    {
        /// 폰트 자산 경로. SDF 계통이 이것으로 아틀라스를 찾는다(D4).
        std::string                             fontPath;
        std::string                             message;
        Mathf::Color4                           color{ DirectX::Colors::Black };
        DirectX::XMFLOAT2                       position{};
        Mathf::Vector2                          maxSize{};
        float                                   fontSize{ 5.f };
        int                                     canvasOrder{ 0 };
        int                                     layerOrder{ 0 };
        SpriteEffects                           filpEffect{ SpriteEffects_None };
        bool                                    stretchX{ false };
        bool                                    stretchY{ false };
        TextAlignment                           alignment{ TextAlignment::Center };
    };

    struct SpriteSheetData
    {
        std::string                             spriteSheetPath{};
        DirectX::XMFLOAT2                       origin{};
        Mathf::Vector3                          position{};
        Mathf::Color4                           color{ 1.f, 1.f, 1.f, 1.f };
        Mathf::Vector2                          scale{ 1.f, 1.f };
        float                                   rotation{ 0.f };
        int                                     canvasOrder{ 0 };
        int                                     layerOrder{ 0 };
        float                                   deltaTime{};
		float                                   frameDuration{ 0.1f };
        SpriteEffects                           filpEffect{ SpriteEffects_None };
        bool                                    isPreview{ false };
        float                                   clipPercent{ 1.f };
        ClipDirection                           clipDirection{ ClipDirection::None };
	};

    explicit UIRenderProxy(ImageComponent* image) noexcept;
    explicit UIRenderProxy(TextComponent* text) noexcept;
	explicit UIRenderProxy(SpriteSheetComponent* sprite) noexcept;
    ~UIRenderProxy();

    int GetCanvasOrder() const {
        return std::visit([](auto&& d) { return d.canvasOrder; }, m_data);
    }

    int GetLayerOrder() const {
        return std::visit([](auto&& d) { return d.layerOrder; }, m_data);
    }

    /// 담고 있는 것(이미지·텍스트·스프라이트시트)을 읽기 전용으로 준다.
    ///
    /// DX12 UI 패스가 이것을 사각형으로 옮긴다. 그리기 자체를 프록시가 하던
    /// 구조(Draw(spriteBatch))는 DX11 전용이라 그쪽에서는 쓸 수 없다 —
    /// 자료만 꺼내 가고 그리기는 패스가 한다.
    const std::variant<ImageData, TextData, SpriteSheetData>& GetData() const
    {
        return m_data;
    }

    const HashedGuid& GetInstanceID() const { return m_instancedID; }
    bool IsEnabled() const { return m_isEnabled; }
	void SetEnabled(bool enable) { m_isEnabled = enable; }

private:
    friend class RenderPassData;
	friend class ProxyCommand;
    std::variant<ImageData, TextData, SpriteSheetData>   m_data;
    std::shared_ptr<Texture>                             m_texture{ nullptr };
    std::shared_ptr<SpriteSheet>                         m_spriteSheet{ nullptr };
    mutable SpriteSheet::SequenceState                   m_sequenceState{};
    HashedGuid			                                 m_instancedID{};
	mutable Mathf::Vector2 								 m_textMeasureSize{ 0.f };
	bool                                                 m_isEnabled{ true };
};
#endif // !DYNAMICCPP_EXPORTS
