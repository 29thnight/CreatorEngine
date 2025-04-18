#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "Canvas.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>
#include "UIComponent.h"

class TextComponent : public UIComponent, public IUpdatable, public Meta::IReflectable<TextComponent>
{
public:
	TextComponent();
	~TextComponent() = default;

	virtual void Update(float tick) override;

	//한글이 안나올시 sfont 제대로 만들었는지 확인
	void SetMessage(std::string_view _message) { message = _message.data(); }
	void LoadFont(SpriteFont* _font) { font = _font; }
	void Draw(SpriteBatch* Sbatch);
	std::string message;

	ReflectionFieldInheritance(TextComponent, UIComponent)
	{
		PropertyField
		({
			meta_property(_isTable)
			meta_property(relpos)
			meta_property(message)
		});

		FieldEnd(TextComponent, PropertyOnlyInheritance)
	};



private:
	bool _isTable = true;
	Mathf::Vector2 pos{ 0,0};
	//상위ui 위치기준 추가값
	Mathf::Vector2 relpos{ 0,0 };
	SpriteFont* font = nullptr;
	DirectX::XMVECTORF32 color = DirectX::Colors::Black;
	float fontSize =5.f;
};

