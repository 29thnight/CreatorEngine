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
	void SetMessage(const std::string& _message) { message = _message; }
	void SetFont(const file::path& path);
	void SetFont(const file::path& path, SpriteFont* _font)
	{
		fontPath = path.string();
		font = _font;
	}
	SpriteFont* GetFont() const { return font; }
	const std::string& GetFontPath() const { return fontPath; }

private:
	friend class UIRenderProxy;
	friend class ProxyCommand;
private:
	SpriteFont* font = nullptr;
	[[Property]]
	std::string fontPath{};
	[[Property]]
	std::string message{};
    [[Property]]
	Mathf::Vector2 relpos{ 0, 0 };
        [[Property]]
        Mathf::Color4 color{};
        [[Property]]
        float fontSize{ 1.f };

        // Calculated in Update: maximum render area from RectTransform's stretch
        Mathf::Vector2 stretchSize{ 0.f, 0.f };
        bool isStretchX{ false };
        bool isStretchY{ false };
};

