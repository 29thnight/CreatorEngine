#include "TextComponent.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"

TextComponent::TextComponent()
{
	m_name = "TextComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<TextComponent>();
	type = UItype::Text;

	
}

void TextComponent::Update(float tick)
{
        if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
        {
                const auto& worldRect = rect->GetWorldRect();
                pos = { worldRect.x, worldRect.y };
        }
        pos += relpos;

        auto  image = GetOwner()->GetComponent<ImageComponent>();
        if (image)
                _layerorder = image->GetLayerOrder();
}

void TextComponent::Draw(std::unique_ptr<SpriteBatch>& sBatch)
{
	if (_layerorder < 0) _layerorder = 0;
	//spriteBatch, message,pos, color, rotat,  Á¤·ÄÆ÷ÀÎÆ® ,size
	if (font)
	{
		font->DrawString(sBatch.get(), message.c_str(), pos, color, 0.0f, DirectX::XMFLOAT2(0, 0), fontSize, SpriteEffects_None, float(float(_layerorder) / MaxOreder));
	}
}
