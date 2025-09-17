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
	void SetMessage(std::string _message) { message = _message; }
	std::string GetTextMessage() { return message; }
	void SetFont(const file::path& path);
	void SetFont(const file::path& path, SpriteFont* _font)
	{
		fontPath = path.string();
		font = _font;
	}
	SpriteFont* GetFont() const { return font; }
	const std::string& GetFontPath() const { return fontPath; }

	Mathf::Color4 GetColor() const { return color; }
	void SetColor(const Mathf::Color4& col) { color = col; }

	float GetAlpha() const { return color.w; }
	void SetAlpha(float alpha) { color.w = alpha; }

	float GetFontSize() const { return fontSize; }
	void SetFontSize(float size) { fontSize = size; }

	Mathf::Vector2 GetRelativePosition() const { return relpos; }
	void SetRelativePosition(const Mathf::Vector2& pos) { relpos = pos; }

	Mathf::Rect GetManualRect() const { return manualRect; }
	void SetManualRect(const Mathf::Rect& rect) { manualRect = rect; }

	bool IsUsingManualRect() const { return useManualRect; }
	void SetUseManualRect(bool use) { useManualRect = use; }

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

    // When true, message bounds are taken from manualRect instead of the parent's RectTransform
    [[Property]]
    bool useManualRect{ false };
    [[Property]]
    Mathf::Rect manualRect{};

    // Calculated in Update: maximum render area from parent RectTransform
    Mathf::Vector2 stretchSize{ 0.f, 0.f };
    bool isStretchX{ false };
    bool isStretchY{ false };
};

