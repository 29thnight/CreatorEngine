#include "TextComponent.h"


TextComponent::TextComponent()
{
}

void TextComponent::Update(float tick)
{
	pos = m_pOwner->m_transform.position;

}

void TextComponent::Draw(SpriteBatch* Sbatch)
{
	//spriteBatch, message,pos, color, rotat,  정렬포인트 ,size
	if(font)
	font->DrawString(Sbatch, message.c_str(), pos, color, 0.0f, DirectX::XMFLOAT2(0, 0), fontSize);
}
