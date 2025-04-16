#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"

#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>

class TextComponent : public Component, public IRenderable, public IUpdatable<TextComponent>, public Meta::IReflectable<TextComponent>
{
public:
	TextComponent();
	~TextComponent() = default;
	
	std::string ToString() const override
	{
		return std::string("UIComponent");
	}

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}
	virtual void Update(float tick) override;
	void SetMessage(std::wstring_view _message) { message = _message; }
	void LoadFont(SpriteFont* _font) { font = _font; }
	void Draw(SpriteBatch* Sbatch);

	std::wstring message;
	ReflectionField(TextComponent, PropertyOnly)
	{
		PropertyField
		({
			meta_property(m_IsEnabled)
			meta_property(message)
			});

		ReturnReflectionPropertyOnly(TextComponent)
	};



private:
	Mathf::Vector2 pos{ 0,0};

	bool m_IsEnabled = true;
	SpriteFont* font = nullptr;
	DirectX::XMVECTORF32 color = DirectX::Colors::Black;
	float fontSize;
};

