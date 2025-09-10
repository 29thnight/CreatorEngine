#include "UIRenderProxy.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "Texture.h"
#include "ShaderSystem.h"
#include <DirectXTK/SpriteFont.h>
#include <algorithm>

UIRenderProxy::UIRenderProxy(ImageComponent* image) noexcept
{
    ImageData data{};
	data.textures   = image->textures;
    data.texture    = image->m_curtexture;
    data.origin     = { image->uiinfo.size.x * 0.5f, image->uiinfo.size.y * 0.5f };
    data.position   = image->pos;
    data.scale      = image->scale;
    data.color      = image->color;
    data.rotation   = image->rotate;
    data.layerOrder = image->GetLayerOrder();
    data.clipDirection = image->clipDirection;
    data.clipPercent   = image->clipPercent;
    m_data          = data;
    m_instancedID   = image->GetInstanceID();
}

UIRenderProxy::UIRenderProxy(TextComponent* text) noexcept
{
    TextData data{};
    data.font       = text->font;
    data.message    = text->message;
    data.color      = text->color;
    data.position   = Mathf::Vector2(text->pos);
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
                    auto size = info.texture->GetImageSize();
                    float width = size.x * info.scale.x;
                    float height = size.y * info.scale.y;
                    float left = info.position.x - info.origin.x * info.scale.x;
                    float top = info.position.y - info.origin.y * info.scale.y;
                    float right = left + width;
                    float bottom = top + height;

                    float percent = std::clamp(info.clipPercent, 0.f, 1.f);
                    switch (info.clipDirection)
                    {
                    case ClipDirection::LeftToRight:
                        right = left + width * percent;
                        break;
                    case ClipDirection::RightToLeft:
                        left = right - width * percent;
                        break;
                    case ClipDirection::TopToBottom:
                        bottom = top + height * percent;
                        break;
                    case ClipDirection::BottomToTop:
                        top = bottom - height * percent;
                        break;
                    default:
                        break;
                    }

                    RECT destRect{
                        static_cast<LONG>(left),
                        static_cast<LONG>(top),
                        static_cast<LONG>(right),
                        static_cast<LONG>(bottom)
                    };
                    DirectX::XMFLOAT2 originScaled{ info.origin.x * info.scale.x, info.origin.y * info.scale.y };
                    spriteBatch->Draw(
                        info.texture->m_pSRV,
                        destRect,
                        nullptr,
                        info.color,
                        info.rotation,
                        originScaled,
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

void UIRenderProxy::SetCustomPixelShader(std::string_view shaderPath)
{
    auto& shader = ShaderSystem->PixelShaders[shaderPath.data()];
    if (!shader.IsCompiled() && !shader.GetShader())
    {
        std::cout << "Failed to load custom pixel shader: " << shaderPath << std::endl;
        return;
    }
	m_customPixelShader = &shader;

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = 16; // 최소 16바이트 단위로 할당
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_customPixelBuffer));
}

void UIRenderProxy::SetCustomPixelBuffer(const std::vector<std::byte>& cpuBuffer)
{
    if (!m_customPixelBuffer)
    {
        std::cout << "Custom pixel buffer is not created." << std::endl;
        return;
    }

	m_customPixelCPUBuffer = cpuBuffer;
}

void UIRenderProxy::UpdateShaderBuffer(ID3D11DeviceContext* deferredContext)
{
    if (m_customPixelBuffer && !m_customPixelCPUBuffer.empty())
    {
		deferredContext->PSSetConstantBuffers(1, 1, m_customPixelBuffer.GetAddressOf());
        deferredContext->UpdateSubresource(m_customPixelBuffer.Get(), 0, nullptr, m_customPixelCPUBuffer.data(), 0, 0);
    }
}
