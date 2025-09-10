#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "UIComponent.h"
#include "ImageComponent.generated.h"

class Texture;
class UIMesh;
class Canvas;
class SpriteSheetComponent : public UIComponent, public RegistableEvent<SpriteSheetComponent>
{
public:
	[[Serializable(Inheritance:UIComponent)]]
	GENERATED_BODY(SpriteSheetComponent)

	void LoadSpriteSheet(std::string_view path);

	virtual void Awake() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;

	ImageInfo	uiinfo{};
	std::shared_ptr<Texture> m_spriteSheetTexture{};
	[[Property]]
	std::string m_spriteSheetPath{};
};