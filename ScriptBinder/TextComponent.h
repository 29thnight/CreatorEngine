#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include <DirectXTK/SpriteBatch.h>
#include "UIComponent.h"

class TextComponent : public meta::identity<TextComponent, UIComponent>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::fontPath>,
           meta::field<&Self::message>,
           meta::field<&Self::relpos>,
           meta::field<&Self::color>,
           meta::field<&Self::manualRect>,
           meta::field<&Self::fontSize>,
           meta::field<&Self::horizontalAlignment>,
           meta::field<&Self::useManualRect>,
           meta::field<&Self::m_textMeasureSize>);
   }
public:
	TextComponent();
	~TextComponent() = default;

	virtual void Awake() override;
	virtual void OnDestroy() override;

	// 트랙 C3(레인 2: UI계) — 가상 Update 오버라이드를 걷어내고 UITickSystem
	// (조밀 벡터, 전용 틱)으로 옮겼다. 등록/해지는 씬 편입/이탈 훅으로 한다
	// (DDOL 안전, 근거는 UITickSystem.h 주석). Awake/OnDestroy는 RenderScene
	// 커맨드 등록·UIManager 캔버스-연결 등록용으로 그대로 둔다(트랙 범위 밖).
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;
	// 옛 Update(float tick)의 본문 그대로 — UITickSystem::Update가 가드를
	// 통과시킨 뒤 호출한다. 이름을 바꾼 이유는 이 시그니처가 Update로 남아
	// 있으면 LifecycleRegistry::MaskOfType이 여전히 Bit_Update를 세워 암묵
	// 구독이 되살아나기 때문이다(Component::Update와 이름이 같아야 감지된다).
	void TickLayout(float tick);

	//한글이 안나올시 sfont 제대로 만들었는지 확인
	void SetMessage(std::string _message) { message = _message; }
	std::string GetTextMessage() { return message; }
	void SetFont(const file::path& path);
	void OnDeserialized(); // CT6-d: 폰트 로드(구 팩토리 분기)

	const std::string& GetFontPath() const { return fontPath; }

	Mathf::Color4 GetColor() const { return color; }
	void SetColor(const Mathf::Color4& col) { color = col; }

	float GetAlpha() const { return color.w; }
	void SetAlpha(float alpha) { color.w = alpha; }

	float GetFontSize() const { return fontSize; }
	void SetFontSize(float size) { fontSize = size; }

	Mathf::Vector2 GetRelativePosition() const { return relpos; }
	void SetRelativePosition(const Mathf::Vector2& pos) { relpos = pos; }

	void SetHorizontalAlignment(TextAlignment alignment) { horizontalAlignment = alignment; }
	TextAlignment GetHorizontalAlignment() const { return horizontalAlignment; }

	Mathf::Rect GetManualRect() const { return manualRect; }
	void SetManualRect(const Mathf::Rect& rect) { manualRect = rect; }

	bool IsUsingManualRect() const { return useManualRect; }
	void SetUseManualRect(bool use) { useManualRect = use; }

	Mathf::Vector2 GetStretchSize() const { return stretchSize; }

private:
	friend class UIRenderProxy;
	friend class ProxyCommand;
private:
	std::string fontPath{};
	std::string message{};
	Mathf::Vector2 relpos{ 0, 0 };
    Mathf::Color4 color{};
    // When true, message bounds are taken from manualRect instead of the parent's RectTransform
    Mathf::Rect manualRect{};
    // Calculated in Update: maximum render area from parent RectTransform
    Mathf::Vector2 stretchSize{ 0.f, 0.f };
    float fontSize{ 1.f };
    // 캔버스에서 물려받은 배율. Update에서 RectTransform으로부터 채워지고,
    // 렌더 프록시가 fontSize에 곱한다. 파생값이라 직렬화하지 않는다(PHASE 7-3).
    float layoutScale{ 1.f };
	TextAlignment horizontalAlignment{ TextAlignment::Center };
    bool useManualRect{ false };
    bool isStretchX{ false };
    bool isStretchY{ false };
public:
	Mathf::Vector2 m_textMeasureSize{ 0.f };
};

