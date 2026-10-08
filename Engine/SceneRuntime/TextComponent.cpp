#include "TextComponent.h"
#include "DataSystem.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RenderScene.h"
#include "UIManager.h"
#include "RectTransformComponent.h"
#include "Entity.h"
#include "UITickSystem.h"
#include "FontAsset.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    bool same_layout_value(float left, float right)
    {
        return left == right || (std::isnan(left) && std::isnan(right));
    }
}

TextComponent::TextComponent()
{
	type = UItype::Text;
}

void TextComponent::OnInitialized()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectTextComponent(this);
		if (renderScene) renderScene->RegisterCommand(this);
	}

	// 레지스트리 등록은 수명의 시작(OnInitialized)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterTextComponent(this);
}

void TextComponent::TickLayout(float tick)
{
    (void)tick;
    if (nullptr == GetOwner())
    {
        return;
    }

    const math::vector3 previousPosition = pos;
    const math::vector2 previousSize = stretchSize;
    const float previousScale = layoutScale;
    const int previousLayer = _layerorder;
    const float currentZ = std::isfinite(pos.z) ? pos.z : 0.f;

    isStretchX = false;
    isStretchY = false;
    stretchSize = { 0.f, 0.f };
    layoutScale = 1.f;

    math::vector2 topLeft{};
    math::vector2 size{};
    bool hasLayout = false;

    if (useManualRect)
    {
        topLeft = { manualRect.x, manualRect.y };
        size = { manualRect.width, manualRect.height };
        hasLayout = true;
    }
    else if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
    {
        const auto& worldRect = rect->GetWorldRect();
        topLeft = { worldRect.x, worldRect.y };
        size = { worldRect.width, worldRect.height };
        hasLayout = true;

        // 글자 크기도 캔버스 배율을 따른다(PHASE 7-3). rect만 줄어들고 글자는
        // 그대로면 화면이 작아질수록 글자가 상자를 뚫고 나온다.
        const float scale = rect->GetLayoutScale();
        layoutScale = std::isfinite(scale) && scale > 0.f ? scale : 0.f;
    }

    if (hasLayout)
    {
        topLeft.x = std::isfinite(topLeft.x) ? topLeft.x : 0.f;
        topLeft.y = std::isfinite(topLeft.y) ? topLeft.y : 0.f;
        size.x = std::isfinite(size.x) ? (std::max)(size.x, 0.f) : 0.f;
        size.y = std::isfinite(size.y) ? (std::max)(size.y, 0.f) : 0.f;
        const float verticalCenter = topLeft.y + size.y * 0.5f;
        float horizontalPos = topLeft.x;

        if (horizontalAlignment == TextAlignment::Center)
        {
            horizontalPos += size.x * 0.5f;
        }
        else if (horizontalAlignment == TextAlignment::Right)
        {
            horizontalPos += size.x;
        }

        const float offsetX = std::isfinite(relpos.x) ? relpos.x * layoutScale : 0.f;
        const float offsetY = std::isfinite(relpos.y) ? relpos.y * layoutScale : 0.f;
        pos = { horizontalPos + offsetX, verticalCenter + offsetY, currentZ };
        stretchSize = size;
        isStretchX = size.x > 0.f;
        isStretchY = size.y > 0.f;
    }
    auto  image = GetOwner()->GetComponent<ImageComponent>();
    if (image)
    {
        _layerorder = image->GetLayerOrder();
    }

    if (previousPosition.x != pos.x || previousPosition.y != pos.y || previousPosition.z != pos.z
        || previousSize.x != stretchSize.x || previousSize.y != stretchSize.y
        || previousScale != layoutScale || previousLayer != _layerorder)
    {
        PublishRenderProxyDirty(ProxyDirty::Transform | ProxyDirty::Payload);
    }

    // Linking is deferred by UIManager, and a Canvas mode/order change can leave
    // the rectangle unchanged. Include its render state in the text publication boundary.
    HashedGuid canvasId{};
    int canvasOrder = 0;
    CanvasRenderMode renderMode = CanvasRenderMode::ScreenSpaceOverlay;
    float planeDistance = 100.f;
    math::matrix4x4 canvasWorld = math::matrix4x4::identity();
    math::vector4 canvasRect{};
    if (auto* canvas = GetOwnerCanvas())
    {
        canvasId = canvas->GetInstanceID();
        canvasOrder = canvas->GetCanvasOrder();
        renderMode = canvas->GetRenderMode();
        planeDistance = canvas->GetPlaneDistance();
        if (auto* owner = canvas->GetOwner())
        {
            canvasWorld = owner->Transform_().GetRenderWorldMatrix();
            if (auto* root = owner->GetComponent<RectTransformComponent>())
            {
                const auto& rect = root->GetWorldRect();
                canvasRect = { rect.x, rect.y, rect.width, rect.height };
            }
        }
    }
    bool canvasChanged = m_layoutCanvasId != canvasId || m_layoutCanvasOrder != canvasOrder
        || m_layoutRenderMode != renderMode || !same_layout_value(m_layoutPlaneDistance, planeDistance)
        || !same_layout_value(m_layoutCanvasRect.x, canvasRect.x)
        || !same_layout_value(m_layoutCanvasRect.y, canvasRect.y)
        || !same_layout_value(m_layoutCanvasRect.z, canvasRect.z)
        || !same_layout_value(m_layoutCanvasRect.w, canvasRect.w);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            canvasChanged = canvasChanged
                || !same_layout_value(m_layoutCanvasWorld(row, column), canvasWorld(row, column));
        }
    }
    m_layoutCanvasId = canvasId;
    m_layoutCanvasOrder = canvasOrder;
    m_layoutRenderMode = renderMode;
    m_layoutPlaneDistance = planeDistance;
    m_layoutCanvasWorld = canvasWorld;
    m_layoutCanvasRect = canvasRect;
    if (canvasChanged)
    {
        PublishRenderProxyDirty(ProxyDirty::Transform | ProxyDirty::Payload);
    }
    PrepareTextLayout();
}

void TextComponent::OnUninitializing()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectTextComponent(this);
		if (renderScene) renderScene->UnregisterCommand(this);
	}

	// 해제는 무조건 한다. 예전에는 씬이 널이면 건너뛰어 레지스트리에 dangling이 남았다.
	UIManagers->UnregisterTextComponent(this);

	// 소속 캔버스 목록에서도 빠진다 — Canvas::Update의 매 프레임 청소를 대체한다(6-4).
	if (Canvas* owner = GetOwnerCanvas())
	{
		owner->RemoveUIObject(GetOwner());
	}
}

// 트랙 C3(레인 2: UI계) — UITickSystem 등록/해지. OnInitialized/OnUninitializing(컴포넌트당
// 1회 게이트)가 아니라 씬 편입/이탈 훅을 쓰는 이유는 UITickSystem.h 상단
// 주석 참조 — DDOL 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다.
// 실제 파괴 경로(PrefabUtility::ApplyComponentDiff·Scene::FlushPendingDestroy)
// 도 OnUninitializing 직전에 OnRemovingFromScene을 먼저 부르므로, 이 시스템에서
// 빠지는 시점이 항상 실 파괴보다 먼저다.
void TextComponent::OnAddedToScene()
{
	UIComponent::OnAddedToScene();
	UITickSystems->RegisterText(this);
	if (HasLifecycleState(State_Initialized) && GetOwner())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->CollectTextComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->RegisterCommand(this);
		}
	}
}

void TextComponent::OnRemovingFromScene()
{
	UITickSystems->UnregisterText(this);
	if (GetOwner() && !GetOwner()->IsDestroyMark())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->UnCollectTextComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->UnregisterCommand(this);
		}
	}
	UIComponent::OnRemovingFromScene();
}

void TextComponent::SetFont(const file::path& path)
{
    // Registered project assets store their relocation-safe identity. Unregistered
    // and engine resource requests retain directories, never a colliding basename.
    FileGuid guid = DataSystems->GetFileGuid(path);
    if (guid == FileGuid{} && path.is_relative() && !path.empty())
    {
        guid = DataSystems->GetFileGuid((PathFinder::Relative() / path).lexically_normal());
    }
    const auto utf8 = path.generic_u8string();
    fontPath = guid != FileGuid{} ? guid.ToString() : std::string(utf8.begin(), utf8.end());
    m_fontLoadAttempted = false;
    m_textLayoutDirty = true;
    PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
}

void TextComponent::OnDeserialized()
{
    // Empty authoring path selects the packaged runtime font. CPU work happens
    // at the scene owner boundary, never in a noexcept render proxy constructor.
    m_fontLoadAttempted = false;
    m_textLayoutDirty = true;
}

void TextComponent::SetMessage(std::string value)
{
    if (message == value)
    {
        return;
    }
    message = std::move(value);
    m_textLayoutDirty = true;
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

void TextComponent::SetColor(const math::color& value)
{
    color = value;
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

void TextComponent::SetAlpha(float alpha)
{
    color.a = std::isfinite(alpha) ? std::clamp(alpha, 0.f, 1.f) : 0.f;
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

void TextComponent::SetFontSize(float size)
{
    fontSize = std::isfinite(size) && size > 0.f ? size : 0.f;
    m_textLayoutDirty = true;
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

void TextComponent::SetRelativePosition(const math::vector2& position)
{
    relpos = position;
    PublishRenderProxyDirty(ProxyDirty::Transform);
}

void TextComponent::SetHorizontalAlignment(TextAlignment alignment)
{
    horizontalAlignment = alignment;
    m_textLayoutDirty = true;
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

void TextComponent::SetManualRect(const math::rect& rect)
{
    manualRect = rect;
    PublishRenderProxyDirty(ProxyDirty::Transform);
}

void TextComponent::SetUseManualRect(bool use)
{
    useManualRect = use;
    PublishRenderProxyDirty(ProxyDirty::Transform);
}

void TextComponent::OnPropertyChanged(std::string_view propertyName, Meta::PropertyChangeSource source)
{
    // Reflection, undo and CLI property edits use the same invalidation as script setters.
    if (propertyName == "fontPath")
    {
        m_fontLoadAttempted = false;
    }
    if (propertyName == "fontPath" || propertyName == "message" || propertyName == "fontSize"
        || propertyName == "horizontalAlignment")
    {
        m_textLayoutDirty = true;
    }
    UIComponent::OnPropertyChanged(propertyName, source);
}

void TextComponent::PrepareTextLayout()
{
    const float requestedSize = fontSize * layoutScale;
    const float pixelSize = std::isfinite(requestedSize) && requestedSize > 0.f ? requestedSize : 0.f;
    const float width = isStretchX && std::isfinite(stretchSize.x) ? (std::max)(stretchSize.x, 0.f) : 0.f;
    const std::uint64_t cacheRevision = DataSystems->GetFontCacheRevision();
    const bool reloadFont = !m_fontLoadAttempted || m_loadedFontPath != fontPath
        || cacheRevision != m_fontCacheRevision;
    const bool retryAtlas = m_textLayout && m_textLayout->retryWhenAtlasAvailable
        && m_font && m_font->CanRetryAtlas();
    if (!reloadFont && !retryAtlas && !m_textLayoutDirty
        && pixelSize == m_layoutPixelSize && width == m_layoutWidth)
    {
        return;
    }

    m_textLayoutDirty = false;
    m_layoutPixelSize = pixelSize;
    m_layoutWidth = width;
    m_fontCacheRevision = cacheRevision;
    std::string error;
    if (reloadFont)
    {
        m_loadedFontPath = fontPath;
        m_fontLoadAttempted = true;
        m_font = DataSystems->LoadFontShared(fontPath, error);
        if (!error.empty())
        {
            Debug::PrintLog(spdlog::level::warn, "Text font '{}': {}", fontPath, error);
            error.clear();
        }
    }

    m_textLayout.reset();
    m_textMeasureSize = { 0.f, 0.f };
    if (m_font && !message.empty() && std::isfinite(pixelSize) && pixelSize > 0.f)
    {
        m_textLayout = m_font->BuildLayout(message, pixelSize, width, horizontalAlignment, error);
        if (m_textLayout)
        {
            m_textMeasureSize = { m_textLayout->width, m_textLayout->height };
        }
    }
    if (!error.empty())
    {
        Debug::PrintLog(spdlog::level::warn, "Text font '{}': {}", fontPath, error);
    }
    PublishRenderProxyDirty(ProxyDirty::Payload);
}

