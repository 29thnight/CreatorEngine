#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "Canvas.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>

class TextComponent : public Component, public IRenderable, public IUpdatable<TextComponent>, public Meta::IReflectable<TextComponent>
{
public:
	TextComponent();
	~TextComponent() = default;
	
	std::string ToString() const override
	{
		return std::string("TextComponent");
	}

	bool IsEnabled() const override
	{
		return m_TIsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_TIsEnabled = able;
	}
	virtual void Update(float tick) override;

	//�ѱ��� �ȳ��ý� sfont ����� ��������� Ȯ��
	void SetMessage(std::string_view _message) { message = _message.data(); }
	void LoadFont(SpriteFont* _font) { font = _font; }
	void Draw(SpriteBatch* Sbatch);
	void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	std::string message;
	ReflectionField(TextComponent, PropertyOnly)
	{
		PropertyField
		({
			meta_property(m_TIsEnabled)
			meta_property(relpos)
			meta_property(message)
			});

		ReturnReflectionPropertyOnly(TextComponent)
	};



private:
	Mathf::Vector2 pos{ 0,0};
	Canvas* ownerCanvas = nullptr;

	//����ui ��ġ���� �߰���
	Mathf::Vector2 relpos{ 0,0 };
	bool m_TIsEnabled = true;
	SpriteFont* font = nullptr;
	DirectX::XMVECTORF32 color = DirectX::Colors::Black;
	float fontSize =5.f;
};

