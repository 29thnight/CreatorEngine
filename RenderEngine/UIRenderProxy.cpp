#include "UIRenderProxy.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "Texture.h"
#include <DirectXTK/SpriteFont.h>

UIRenderProxy::UIRenderProxy(ImageComponent* image) noexcept
{
    ImageData data{};
	data.textures = image->textures;
    data.texture    = image->m_curtexture;
    data.origin     = { image->uiinfo.size.x * 0.5f, image->uiinfo.size.y * 0.5f };
    data.position   = image->pos;
    data.scale      = image->scale;
    if (auto owner  = image->GetOwner())
    {
        float pitch, yaw, roll;
        Mathf::QuaternionToEular(owner->m_transform.rotation, pitch, yaw, roll);
        data.rotation = roll;
    }
    data.layerOrder = image->GetLayerOrder();
    m_data          = data;
    m_instancedID   = image->GetInstanceID();
}

UIRenderProxy::UIRenderProxy(TextComponent* text) noexcept
{
    TextData data{};
    data.font       = text->font;
    data.message    = text->message;
    data.color      = text->color;
    data.position   = text->pos;
    data.fontSize   = text->fontSize;
    data.layerOrder = text->GetLayerOrder();
    m_data          = data;
    m_instancedID   = text->GetInstanceID();
}

UIRenderProxy::~UIRenderProxy()
{
    std::visit(
        [](auto& info)
        {
            using T = std::decay_t<decltype(info)>;
            if constexpr (std::is_same_v<T, ImageData>)
            {
                info.texture.reset();
                info.textures.clear();
            }
            else if constexpr (std::is_same_v<T, TextData>)
            {
                info.font = nullptr;
                info.message.clear();
            }
        },
        m_data);
}

void UIRenderProxy::Draw(std::unique_ptr<DirectX::SpriteBatch>& spriteBatch) const
{
    std::visit(
        [&](auto const& info)
        {
            using T = std::decay_t<decltype(info)>;
            if constexpr (std::is_same_v<T, ImageData>)
            {
                if (info.texture)
                {
                    spriteBatch->Draw(
                        info.texture->m_pSRV,
                        { info.position.x, info.position.y },
                        nullptr,
                        DirectX::Colors::White,
                        info.rotation,
                        { info.origin.x, info.origin.y },
                        info.scale,
                        DirectX::SpriteEffects_None,
                        static_cast<float>(info.layerOrder) / MaxOreder);
                }
            }
            else if constexpr (std::is_same_v<T, TextData>)
            {
                if (info.font)
                {
                    info.font->DrawString(
                        spriteBatch.get(),
                        info.message.c_str(),
                        { info.position.x, info.position.y },
                        info.color,
                        0.0f,
                        DirectX::XMFLOAT2(0, 0),
                        info.fontSize,
                        DirectX::SpriteEffects_None,
                        static_cast<float>(info.layerOrder) / MaxOreder);
                }
            }
        },
        m_data);
}

void UIRenderProxy::DestroyProxy()
{
    RenderScene::RegisteredDestroyUIProxyGUIDs.push(m_instancedID);
}