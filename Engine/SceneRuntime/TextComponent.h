#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#include <mathematics/matrix4x4.hpp>
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include <mathematics/color.hpp>
#include <mathematics/rect.hpp>
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include "UIComponent.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"

class FontAsset;
struct TextLayout;

class [[reflgen::reflect, reflgen::public_methods]] TextComponent : public meta::identity<TextComponent, UIComponent>
{
   friend struct reflgen::access;
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        UIComponent::gc_trace(tracer);
    }

public:
	TextComponent();
	~TextComponent() = default;

	[[reflgen::script_internal]]
	virtual void OnInitialized() override;
	[[reflgen::script_internal]]
	virtual void OnUninitializing() override;

	// 트랙 C3(레인 2: UI계) — 가상 Update 오버라이드를 걷어내고 UITickSystem
	// (조밀 벡터, 전용 틱)으로 옮겼다. 등록/해지는 씬 편입/이탈 훅으로 한다
	// (DDOL 안전, 근거는 UITickSystem.h 주석). OnInitialized/OnUninitializing은 RenderScene
	// 커맨드 등록·UIManager 캔버스-연결 등록용으로 그대로 둔다(트랙 범위 밖).
	[[reflgen::script_internal]]
	void OnAddedToScene() override;
	[[reflgen::script_internal]]
	void OnRemovingFromScene() override;
	// 옛 Update(float tick)의 본문 그대로 — UITickSystem::Update가 가드를
	// 통과시킨 뒤 호출한다. 이름을 바꾼 것은 트랙 C3 당시 이 시그니처가 Update로
	// 남아 있으면 오버라이드 감지에 걸려 암묵 구독이 되살아났기 때문이다. 그
	// 메커니즘은 C3 완결로 사라졌다 — Component의 가상 틱 3종과 Bit_Update가
	// 함께 철거됐다.
	void TickLayout(float tick);

	// UTF-8 is laid out on the scene owner thread; render snapshots only share
	// immutable glyph geometry and atlas owners.
	void SetMessage(std::string value);
	std::string GetTextMessage() { return message; }
	void SetFont(const std::filesystem::path& path);
	[[reflgen::script_internal]]
	void OnDeserialized();
	[[reflgen::script_internal]]
	void OnPropertyChanged(std::string_view propertyName, Meta::PropertyChangeSource source) override;
	void PrepareTextLayout();
	const own::shared_owner<const TextLayout>& GetTextLayout() const { return m_textLayout; }

	const std::string& GetFontPath() const { return fontPath; }

	math::color GetColor() const { return color; }
	void SetColor(const math::color& col);

	float GetAlpha() const { return color.a; }
	void SetAlpha(float alpha);

	float GetFontSize() const { return fontSize; }
	void SetFontSize(float size);

	math::vector2 GetRelativePosition() const { return relpos; }
	void SetRelativePosition(const math::vector2& position);

	void SetHorizontalAlignment(TextAlignment alignment);
	TextAlignment GetHorizontalAlignment() const { return horizontalAlignment; }

	math::rect GetManualRect() const { return manualRect; }
	void SetManualRect(const math::rect& rect);

	bool IsUsingManualRect() const { return useManualRect; }
	void SetUseManualRect(bool use);

	math::vector2 GetStretchSize() const { return stretchSize; }

private:
	friend class UIRenderProxy;
	friend class ProxyCommand;
private:
	std::string fontPath{};
	std::string message{};
	math::vector2 relpos{ 0, 0 };
    math::color color{};
    // When true, message bounds are taken from manualRect instead of the parent's RectTransform
    math::rect manualRect{};

    // Calculated in Update: maximum render area from parent RectTransform
    [[reflgen::ignore]]
    math::vector2 stretchSize{ 0.f, 0.f };

    // Pixel height before CanvasScaler. Existing authored numeric values remain unchanged.
    float fontSize{ 32.f };

    // 캔버스에서 물려받은 배율. Update에서 RectTransform으로부터 채워지고,
    // 렌더 프록시가 fontSize에 곱한다. 파생값이라 직렬화하지 않는다(PHASE 7-3).
    [[reflgen::ignore]]
    float layoutScale{ 1.f };

	TextAlignment horizontalAlignment{ TextAlignment::Center };
    bool useManualRect{ false };

    [[reflgen::ignore]]
    bool isStretchX{ false };

    [[reflgen::ignore]]
    bool isStretchY{ false };

    [[reflgen::ignore]]
    own::shared_owner<FontAsset> m_font;
    [[reflgen::ignore]]
    own::shared_owner<const TextLayout> m_textLayout;
    [[reflgen::ignore]]
    std::string m_loadedFontPath;
    [[reflgen::ignore]]
    std::uint64_t m_fontCacheRevision{};
    [[reflgen::ignore]]
    float m_layoutPixelSize{};
    [[reflgen::ignore]]
    float m_layoutWidth{};
    [[reflgen::ignore]]
    bool m_textLayoutDirty{ true };
    [[reflgen::ignore]]
    bool m_fontLoadAttempted{ false };
    [[reflgen::ignore]]
    HashedGuid m_layoutCanvasId{};
    [[reflgen::ignore]]
    int m_layoutCanvasOrder{};
    [[reflgen::ignore]]
    CanvasRenderMode m_layoutRenderMode{ CanvasRenderMode::ScreenSpaceOverlay };
    [[reflgen::ignore]]
    float m_layoutPlaneDistance{ 100.f };
    [[reflgen::ignore]]
    math::matrix4x4 m_layoutCanvasWorld{ math::matrix4x4::identity() };
    [[reflgen::ignore]]
    math::vector4 m_layoutCanvasRect{};
public:
	[[reflgen::ignore]]
	math::vector2 m_textMeasureSize{ 0.f };
};
