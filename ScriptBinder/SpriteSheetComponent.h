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
