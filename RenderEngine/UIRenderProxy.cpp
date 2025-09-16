#include "UIRenderProxy.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "Texture.h"
#include "ShaderSystem.h"
#include "SpriteSheet.h"
#include "SpriteSheetComponent.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXMath.h>
#include <algorithm>

// Clamps percent to [0, 1] and calculates source and destination rectangles based on the clipping direction.
inline bool CalculateClippedRects(
    ClipDirection dir,
    float percent,
    LONG texW, LONG texH,
    float left, float top, float right, float bottom,
    float scaleX, float scaleY,
    RECT& outSrc, RECT& outDst)
{
    percent = std::clamp(percent, 0.f, 1.f);
    LONG cutW = static_cast<LONG>(std::floor(texW * percent));
    LONG cutH = static_cast<LONG>(std::floor(texH * percent));

    switch (dir)
    {
    case ClipDirection::LeftToRight:
        if (cutW <= 0) return false;
        outSrc = { 0, 0, cutW, texH };
        outDst = {
            static_cast<LONG>(std::lround(left)),
            static_cast<LONG>(std::lround(top)),
            static_cast<LONG>(std::lround(left + cutW * scaleX)),
            static_cast<LONG>(std::lround(top + texH * scaleY))
        };
        return true;

    case ClipDirection::RightToLeft:
        if (cutW <= 0) return false;
        outSrc = { texW - cutW, 0, texW, texH };
        outDst = {
            static_cast<LONG>(std::lround(right - cutW * scaleX)),
            static_cast<LONG>(std::lround(top)),
            static_cast<LONG>(std::lround(right)),
            static_cast<LONG>(std::lround(top + texH * scaleY))
        };
        return true;

    case ClipDirection::TopToBottom:
        if (cutH <= 0) return false;
        outSrc = { 0, 0, texW, cutH };
        outDst = {
            static_cast<LONG>(std::lround(left)),
            static_cast<LONG>(std::lround(top)),
            static_cast<LONG>(std::lround(left + texW * scaleX)),
            static_cast<LONG>(std::lround(top + cutH * scaleY))
        };
        return true;

    case ClipDirection::BottomToTop:
        if (cutH <= 0) return false;
        outSrc = { 0, texH - cutH, texW, texH };
        outDst = {
            static_cast<LONG>(std::lround(left)),
            static_cast<LONG>(std::lround(bottom - cutH * scaleY)),
            static_cast<LONG>(std::lround(left + texW * scaleX)),
            static_cast<LONG>(std::lround(bottom))
        };
        return true;

    case ClipDirection::None:
    default:
        outSrc = { 0, 0, texW, texH };
        outDst = {
            static_cast<LONG>(std::lround(left)),
            static_cast<LONG>(std::lround(top)),
            static_cast<LONG>(std::lround(right)),
            static_cast<LONG>(std::lround(bottom))
        };
        return true;
    }
}
//==================================================================
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
    data.maxSize    = text->stretchSize;
    data.stretchX   = text->isStretchX;
    data.stretchY   = text->isStretchY;
    m_data          = data;
    m_instancedID   = text->GetInstanceID();
}

UIRenderProxy::UIRenderProxy(SpriteSheetComponent* sprite) noexcept
{
    SpriteSheetData data{};
    m_texture               = sprite->m_spriteSheetTexture;
    m_spriteSheet           = std::make_shared<SpriteSheet>();
	data.spriteSheetPath    = sprite->m_spriteSheetPath;
    data.origin             = { sprite->uiinfo.size.x * 0.5f, sprite->uiinfo.size.y * 0.5f };
    data.position           = sprite->pos;
    data.scale              = sprite->scale;
    data.layerOrder         = sprite->GetLayerOrder();
    m_sequenceState.loop    = sprite->m_isLoop;
	data.frameDuration      = sprite->m_frameDuration;

    if (m_texture && !data.spriteSheetPath.empty())
    {
        try
        {
		    file::path path = PathFinder::Relative("SpriteSheets\\") / 
                file::path(data.spriteSheetPath).filename().replace_extension(".txt");

            m_spriteSheet->Load(m_texture->m_pSRV, path.c_str());
        }
        catch (const std::exception& e)
        {
            std::cout << "Failed to load sprite sheet from path: " << data.spriteSheetPath << "\nError: " << e.what() << std::endl;
		}
    }

    m_data                  = data;
	m_instancedID           = sprite->GetInstanceID();
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
            else if constexpr (std::is_same_v<T, SpriteSheetData>)
            {
                info.spriteSheetPath.clear();
			}
        },
        m_data);

    m_spriteSheet.reset();
	m_texture.reset();
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
                    LONG texW = static_cast<LONG>(size.x);
                    LONG texH = static_cast<LONG>(size.y);

                    float left = info.position.x - info.origin.x * info.scale.x;
                    float top = info.position.y - info.origin.y * info.scale.y;
                    float right = left + texW * info.scale.x;
                    float bottom = top + texH * info.scale.y;

                    RECT src{}, dst{};
                    if (CalculateClippedRects(
                        info.clipDirection,
                        info.clipPercent,
                        texW, texH,
                        left, top, right, bottom,
                        info.scale.x, info.scale.y,
                        src, dst))
                    {
                        spriteBatch->Draw(
                            info.texture->m_pSRV,
                            dst,
                            &src,
                            info.color,
                            info.rotation,
                            info.origin,
                            DirectX::SpriteEffects_None,
                            static_cast<float>(info.layerOrder) / MaxOreder);
                    }
                }
            }
            else if constexpr (std::is_same_v<T, TextData>)
            {
                if (info.font)
                {
                    float scale = info.fontSize;
                    if (info.stretchX || info.stretchY)
                    {
                        DirectX::XMVECTOR sizeVec = info.font->MeasureString(info.message.c_str());
                        DirectX::XMFLOAT2 size{};
                        DirectX::XMStoreFloat2(&size, sizeVec);
                        float width = size.x * scale;
                        float height = size.y * scale;
                        float factor = 1.f;
                        if (info.stretchX && width > info.maxSize.x)
                            factor = std::min(factor, info.maxSize.x / width);
                        if (info.stretchY && height > info.maxSize.y)
                            factor = std::min(factor, info.maxSize.y / height);
                        scale *= factor;
                    }

                    info.font->DrawString(
                        spriteBatch.get(),
                        info.message.c_str(),
                        { info.position.x, info.position.y },
                        info.color,
                        0.0f,
                        DirectX::XMFLOAT2(0, 0),
                        scale,
                        DirectX::SpriteEffects_None,
                        static_cast<float>(info.layerOrder) / MaxOreder);
                }
            }
            else if constexpr (std::is_same_v<T, SpriteSheetData>)
            {
                if (m_texture && m_spriteSheet)
                {
                    DirectX::XMFLOAT2 pos{ info.position.x, info.position.y };
                    auto size = m_texture->GetImageSize();
					float deltaTime = info.isPreview ? Time->GetElapsedSeconds() : info.deltaTime;
                    m_spriteSheet->DrawSequential(spriteBatch.get(),
                        pos,
                        deltaTime,
						info.frameDuration,
                        m_sequenceState,
                        DirectX::Colors::White,
                        0.f,
                        1.f,
                        DirectX::SpriteEffects_None,
						static_cast<float>(info.layerOrder) / MaxOreder
                    );
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

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
    if (FAILED(D3DReflect(shader.GetBufferPointer(),
        shader.GetBufferSize(),
        IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(reflector.GetAddressOf()))))
    {
        std::cout << "Failed to reflect pixel shader: " << shaderPath << std::endl;
        return;
    }

    auto* constantBuffer = reflector->GetConstantBufferByIndex(0);
    D3D11_SHADER_BUFFER_DESC cbDesc{};
    if (constantBuffer && SUCCEEDED(constantBuffer->GetDesc(&cbDesc)))
    {
        m_customPixelBufferSize = cbDesc.Size;
    }
    else
    {
        m_customPixelBufferSize = 16; // 기본 최소 크기
    }

    m_customPixelBufferSize = ((m_customPixelBufferSize + 15) / 16) * 16;

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = m_customPixelBufferSize; // 최소 16바이트 단위로 할당
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
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
    //?????
	m_customPixelCPUBuffer = cpuBuffer;
}

void UIRenderProxy::UpdateShaderBuffer(ID3D11DeviceContext* deferredContext)
{
    if (m_customPixelBuffer && m_customPixelCPUBuffer.size() == m_customPixelBufferSize)
    {
		deferredContext->PSSetConstantBuffers(1, 1, m_customPixelBuffer.GetAddressOf());
        deferredContext->UpdateSubresource(m_customPixelBuffer.Get(), 0, nullptr, m_customPixelCPUBuffer.data(), 0, 0);
    }
}
