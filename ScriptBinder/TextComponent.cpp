#include "TextComponent.h"
#include "ImageComponent.h"

TextComponent::TextComponent()
{
	m_name = "TextComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<TextComponent>();
	type = UItype::Text;

	
}

void TextComponent::Update(float tick)
{
	pos = Mathf::Vector2(m_pOwner->m_transform.position);
	pos += relpos;
	//m_IsEnabled = _isTable;
	
	auto  image = GetOwner()->GetComponent<ImageComponent>();
	if (image)
		_layerorder = image->GetLayerOrder();
}

void TextComponent::Draw(std::unique_ptr<SpriteBatch>& sBatch)
{
	if (_layerorder < 0) _layerorder = 0;
	//spriteBatch, message,pos, color, rotat,  정렬포인트 ,size
	if (font)
	{
		font->DrawString(sBatch.get(), message.c_str(), pos, color, 0.0f, DirectX::XMFLOAT2(0, 0), fontSize, SpriteEffects_None, float(float(_layerorder) / MaxOreder));
	}
}
