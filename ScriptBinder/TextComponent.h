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

	virtual void Update(float tick) override;

	//한글이 안나올시 sfont 제대로 만들었는지 확인
	void SetMessage(std::string_view _message) { message = _message.data(); }
	void LoadFont(SpriteFont* _font) { font = _font; }
	void Draw(std::unique_ptr<SpriteBatch>& sBatch);
	[[Property]]
	std::string message;

private:
	Mathf::Vector2 pos{ 0,0};
	//상위ui 위치기준 추가값
    [[Property]]
	Mathf::Vector2 relpos{ 0,0 };
	SpriteFont* font = nullptr;
	DirectX::XMVECTORF32 color = DirectX::Colors::Black;
	float fontSize =5.f;
};

