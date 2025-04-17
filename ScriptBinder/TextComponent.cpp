#include "TextComponent.h"


TextComponent::TextComponent()
{
}

void TextComponent::Update(float tick)
{
	pos = m_pOwner->m_transform.position;
	pos += relpos;

}

void TextComponent::Draw(SpriteBatch* Sbatch)
{
	XMFLOAT3 worldPos = { pos.x, pos.y, 1.f }; // z���� ���� ������ ���� �޶���
	XMVECTOR posVec = XMLoadFloat3(&worldPos);
	//spriteBatch, message,pos, color, rotat,  ��������Ʈ ,size
	if (font)
	{
		font->DrawString(Sbatch, message.c_str(), posVec, color, 0.0f, {},  fontSize);
		//font->DrawString(Sbatch, message.c_str(), pos, color, 0.0f, DirectX::XMFLOAT2(0, 0), fontSize, SpriteEffects_None, 1.0f);
	}
}
