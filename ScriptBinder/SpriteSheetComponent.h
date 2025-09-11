#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "UIComponent.h"
#include "ImageComponent.generated.h"
#include "SpriteSheetComponent.generated.h"

class Texture;
class UIMesh;
class Canvas;
class SpriteSheetComponent : public UIComponent, public RegistableEvent<SpriteSheetComponent>
{
public:
   ReflectSpriteSheetComponent
	[[Serializable(Inheritance:UIComponent)]]
	GENERATED_BODY(SpriteSheetComponent)

	void LoadSpriteSheet(const file::path& path);

	virtual void Awake() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;

	ImageInfo				 uiinfo{};
	std::shared_ptr<Texture> m_spriteSheetTexture{};
	[[Property]]
	std::string				 m_spriteSheetPath{};
	[[Property]]
	float                    m_frameDuration{ 0.1f };
	[[Property]]
	bool                     m_isLoop{ true };
	[[Property]]
	bool                     m_isPreview{ false };
	float                    m_deltaTime{};
};
