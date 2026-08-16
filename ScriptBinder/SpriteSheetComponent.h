#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "UIComponent.h"
#include "Navigation.h"

class Texture;
class UIMesh;
class Canvas;
class SpriteSheetComponent : public UIComponent
{
public:
   static consteval auto describe()
   {
       return meta::describe<SpriteSheetComponent>(
           meta::base<UIComponent>(),
           meta::member<&SpriteSheetComponent::m_spriteSheetPath>(),
           meta::member<&SpriteSheetComponent::m_frameDuration>(),
           meta::member<&SpriteSheetComponent::clipPercent>(),
           meta::member<&SpriteSheetComponent::clipDirection>(),
           meta::member<&SpriteSheetComponent::m_isLoop>(),
           meta::member<&SpriteSheetComponent::m_isPreview>());
   }
	GENERATED_BODY(SpriteSheetComponent)

	void LoadSpriteSheet(const file::path& path);
	void OnDeserialized(); // CT6-d: 시트 로드 + 프리뷰 해제(구 팩토리 분기)


	virtual void Awake() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;

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
