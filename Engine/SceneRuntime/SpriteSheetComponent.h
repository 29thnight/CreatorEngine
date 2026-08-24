#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "UIComponent.h"
#include "Navigation.h"

class Texture;
class UIMesh;
class Canvas;
class SpriteSheetComponent : public meta::identity<SpriteSheetComponent, UIComponent>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_spriteSheetPath>,
           meta::field<&Self::m_frameDuration>,
           meta::field<&Self::clipPercent>,
           meta::field<&Self::clipDirection>,
           meta::field<&Self::m_isLoop>,
           meta::field<&Self::m_isPreview>);
   }
public:
	SpriteSheetComponent() = default;

	void LoadSpriteSheet(const file::path& path);
	void OnDeserialized(); // CT6-d: 시트 로드 + 프리뷰 해제(구 팩토리 분기)


	virtual void OnInitialized() override;
	virtual void OnUninitializing() override;

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

	ImageInfo				 uiinfo{};
	std::shared_ptr<Texture> m_spriteSheetTexture{};
	std::string				 m_spriteSheetPath{};
	float                    m_frameDuration{ 0.1f };
	float                    m_deltaTime{};
	float                    clipPercent{ 1.f };
	ClipDirection            clipDirection{ ClipDirection::None };
	bool                     m_isLoop{ true };
	bool                     m_isPreview{ false };
};
