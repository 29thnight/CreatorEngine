// UIRenderProxy의 컴포넌트 읽기 생성자들 (PHASE 4-2 C1).
//
// 프록시 타입은 렌더 소유(UIRenderProxy.h), 컴포넌트 -> 프록시 변환은 게임플레이
// 소유가 경계 원칙이다. Draw 계열 렌더 로직은 RenderEngine/UIRenderProxy.cpp에 남는다.
#include "UIRenderProxy.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "Texture.h"
#include "SpriteSheet.h"

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

            m_spriteSheet->Load(path.c_str());
        }
        catch (const std::exception& e)
        {
            std::cout << "Failed to load sprite sheet from path: " << data.spriteSheetPath << "\nError: " << e.what() << std::endl;
		}
    }

    m_data                  = data;
	m_instancedID           = sprite->GetInstanceID();
}
