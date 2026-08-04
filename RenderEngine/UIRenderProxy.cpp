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

// 클리핑 계산은 UIClipping.h로 옮겼다. DX12 UI 패스가 같은 계산을
// 해야 하는데, 두 곳에 따로 두면 하나가 틀려도 알 수 없다.
#include "UIClipping.h"

//==================================================================
UIRenderProxy::UIRenderProxy(ImageComponent* image) noexcept
{
    auto* canvas = image->GetOwnerCanvas();

    ImageData data{};
	data.textures   = image->textures;
    data.texture    = image->m_curtexture;
    data.origin     = image->origin;
    data.position   = image->pos;
    data.scale      = image->scale;
    data.color      = image->color;
    data.rotation   = image->rotate;
    data.layerOrder = image->GetLayerOrder();
    data.clipDirection = image->clipDirection;
    data.clipPercent   = image->clipPercent;
	data.filpEffect = (SpriteEffects)image->uiEffects;
    if (canvas)
    {
        data.canvasOrder = canvas->GetCanvasOrder();
    }
    m_data          = data;
    m_instancedID   = image->GetInstanceID();
}

UIRenderProxy::UIRenderProxy(TextComponent* text) noexcept
{
    auto* canvas = text->GetOwnerCanvas();

    TextData data{};
    data.font = text->font;
    data.message = text->message;
    data.color = text->color;

    data.position = { text->pos.x, text->pos.y };
    // 캔버스 배율은 rect뿐 아니라 글자 크기에도 걸려야 한다(PHASE 7-3).
    data.fontSize = text->fontSize * text->layoutScale;
    if (canvas)
    {
        data.canvasOrder = canvas->GetCanvasOrder();
    }
    data.layerOrder = text->GetLayerOrder();
    data.maxSize = text->stretchSize;
    data.stretchX = text->isStretchX;
    data.stretchY = text->isStretchY;
    data.alignment = text->GetHorizontalAlignment();
    data.filpEffect = (SpriteEffects)text->uiEffects;
    m_data = data;
    m_instancedID = text->GetInstanceID();
}

UIRenderProxy::UIRenderProxy(SpriteSheetComponent* sprite) noexcept
{
    auto* canvas = sprite->GetOwnerCanvas();

    SpriteSheetData data{};
    m_texture               = sprite->m_spriteSheetTexture;
    m_spriteSheet           = std::make_shared<SpriteSheet>();
	data.spriteSheetPath    = sprite->m_spriteSheetPath;
    data.origin             = { sprite->uiinfo.size.x * 0.5f, sprite->uiinfo.size.y * 0.5f };
    data.position           = sprite->pos;
    data.scale              = sprite->scale;
    data.filpEffect = (SpriteEffects)sprite->uiEffects;
    data.clipDirection = sprite->clipDirection;
    data.clipPercent = sprite->clipPercent;
    if (canvas)
    {
        data.canvasOrder = canvas->GetCanvasOrder();
    }
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

                    // position은 rect의 중심(PHASE 7-6). origin은 텍스처 중심이고
                    // scale은 rect크기/텍스처크기이므로 origin*scale은 rect 크기의 절반이다.
                    // 따라서 아래 네 값은 화면 좌표 그대로의 rect다 — CalculateClippedRects가
                    // 이것을 실제 사각형으로 보고 계산하므로 그 전제가 이제 참이 된다.
                    const float halfWidth = info.origin.x * info.scale.x;
                    const float halfHeight = info.origin.y * info.scale.y;

                    const float left = info.position.x - halfWidth;
                    const float top = info.position.y - halfHeight;
                    const float right = left + texW * info.scale.x;
                    const float bottom = top + texH * info.scale.y;

                    ClippedSource src{};
                    ClippedDestination dst{};
                    if (CalculateClippedRects(
                        info.clipDirection,
                        info.clipPercent,
                        texW, texH,
                        left, top, right, bottom,
                        info.scale.x, info.scale.y,
                        src, dst))
                    {
                        // SpriteBatch에 넘기는 사각형의 좌상단은 "그려질 위치"가 아니라
                        // "origin이 놓일 위치"다. 그래서 화면상의 사각형에 rect 크기의
                        // 절반을 더해 앵커 좌표로 옮긴다.
                        //
                        // 이 값은 예전 코드가 pivot 0.5에서 넘기던 것과 정확히 같다
                        // (예전: pos = x + w, left = pos - w/2 = x + w/2 · 지금:
                        //  pos = x + w/2, left = x, 앵커 = x + w/2). 즉 지금 화면에
                        // 제대로 나오는 그 값을 pivot과 무관하게 항상 만든다.
                        const RECT anchored{
                            static_cast<LONG>(std::lround(dst.left + halfWidth)),
                            static_cast<LONG>(std::lround(dst.top + halfHeight)),
                            static_cast<LONG>(std::lround(dst.right + halfWidth)),
                            static_cast<LONG>(std::lround(dst.bottom + halfHeight)) };

                        // SpriteBatch는 RECT를 받는다. ClippedSource와 담는
                        // 것은 같지만 형을 맞춰 넘긴다 — 같은 배치라고 믿고
                        // 캐스트하면 한쪽 정의가 바뀔 때 조용히 깨진다.
                        const RECT srcRect{ src.left, src.top, src.right, src.bottom };

                        spriteBatch->Draw(
                            info.texture->m_pSRV,
                            anchored,
                            &srcRect,
                            info.color,
                            info.rotation,
                            info.origin,
                            info.filpEffect,
                            static_cast<float>(info.layerOrder) / MaxOreder);
                    }
                }
            }
            else if constexpr (std::is_same_v<T, TextData>)
            {
                if (info.font)
                {
                    DirectX::XMVECTOR sizeVec = info.font->MeasureString(info.message.c_str());
                    DirectX::XMFLOAT2 size{};
                    DirectX::XMStoreFloat2(&size, sizeVec);
					m_textMeasureSize = Mathf::Vector2(size.x, size.y);

                    float scale = info.fontSize;
                    if (info.stretchX || info.stretchY)
                    {
                        float width = size.x * scale;
                        float height = size.y * scale;
                        float factor = 1.f;
                        if (info.stretchX && width > info.maxSize.x)
                            factor = std::min(factor, info.maxSize.x / width);
                        if (info.stretchY && height > info.maxSize.y)
                            factor = std::min(factor, info.maxSize.y / height);
                        scale *= factor;
                    }

                    DirectX::XMFLOAT2 origin{};
                    DirectX::XMFLOAT2 drawPosition{ info.position.x, info.position.y };

                    switch (info.alignment)
                    {
                    case TextAlignment::Left:
                        origin = { 0.f, size.y * 0.5f };
                        break;
                    case TextAlignment::Center:
                    default:
                        origin = { size.x * 0.5f, size.y * 0.5f };
                        break;
                    }

                    info.font->DrawString(
                        spriteBatch.get(),
                        info.message.c_str(),
                        drawPosition,
                        info.color,
                        0.0f,
                        origin,
                        scale,
                        info.filpEffect,
                        static_cast<float>(info.layerOrder) / MaxOreder);
                }
            }
            else if constexpr (std::is_same_v<T, SpriteSheetData>)
            {
                if (m_texture && m_spriteSheet)
                {
                    DirectX::XMFLOAT2 pos{ info.position.x, info.position.y };
                    DirectX::XMFLOAT2 spriteScale{ info.scale.x, info.scale.y };
                    float deltaTime = info.isPreview ? Time->GetElapsedSeconds() : info.deltaTime;
                    if (info.clipDirection == ClipDirection::None || info.clipPercent >= 1.f)
                    {
                        m_spriteSheet->DrawSequential(
                            spriteBatch.get(),
                            pos,
                            deltaTime,
                            info.frameDuration,
                            m_sequenceState,
                            DirectX::Colors::White,
                            0.f,
                            spriteScale,
                            info.filpEffect,
                            static_cast<float>(info.layerOrder) / MaxOreder);
                    }
                    else
                    {
                        m_spriteSheet->DrawSequential(
                            spriteBatch.get(),
                            pos,
                            deltaTime,
                            info.frameDuration,
                            m_sequenceState,
                            info.clipDirection,
                            info.clipPercent,
                            DirectX::Colors::White,
                            0.f,
                            spriteScale,
                            info.filpEffect,
                            static_cast<float>(info.layerOrder) / MaxOreder);
                    }
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
    if (!deferredContext) return;
    if (!m_customPixelBuffer) {
        std::cout << "Custom pixel buffer is not created." << std::endl;
        return;
    }
    if (m_customPixelCPUBuffer.size() != m_customPixelBufferSize) return;

    // 1) 로컬 ComPtr로 참조 획득 -> 레퍼런스 카운트 보장
    Microsoft::WRL::ComPtr<ID3D11Buffer> bufferRef = m_customPixelBuffer;

    // 2) (선택) 호출 중 외부에서 CPU 버퍼를 변경할 수 있다면 로컬 복사
    std::vector<std::byte> cpuCopy = m_customPixelCPUBuffer;

    // 3) 안전하게 raw 포인터로 전달
    ID3D11Buffer* bufPtr = bufferRef.Get();
    deferredContext->PSSetConstantBuffers(1, 1, &bufPtr);
    deferredContext->UpdateSubresource(bufferRef.Get(), 0, nullptr, cpuCopy.data(), 0, 0);

  //  if (m_customPixelBuffer && m_customPixelCPUBuffer.size() == m_customPixelBufferSize)
  //  {
		//deferredContext->PSSetConstantBuffers(1, 1, m_customPixelBuffer.GetAddressOf());
  //      deferredContext->UpdateSubresource(m_customPixelBuffer.Get(), 0, nullptr, m_customPixelCPUBuffer.data(), 0, 0);
  //  }
}
