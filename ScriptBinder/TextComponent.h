#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "Canvas.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>
#include "UIComponent.h"
#include "TextComponent.generated.h"

class TextComponent : public UIComponent, public RegistableEvent<TextComponent>
{
public:
   ReflectTextComponent
    [[Serializable(Inheritance:UIComponent)]]
	TextComponent();
	~TextComponent() = default;

	virtual void Awake() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;

	//한글이 안나올시 sfont 제대로 만들었는지 확인
	void SetMessage(std::string_view _message) { message = _message.data(); }
	void SetFont(SpriteFont* _font) { font = _font; }

private:
	friend class UIRenderProxy;
	friend class ProxyCommand;
private:
	SpriteFont* font = nullptr;
	[[Property]]
	std::string message;
    [[Property]]
	Mathf::Vector2 relpos{ 0,0 };
	[[Property]]
	Mathf::Color4 color{};
	[[Property]]
	float fontSize = 1.f;
};

