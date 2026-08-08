#include "TextComponent.h"
#include "DataSystem.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RenderScene.h"
#include "UIManager.h"
#include "RectTransformComponent.h"
#include "GameObject.h"

TextComponent::TextComponent()
{
	m_name = "TextComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<TextComponent>();
	type = UItype::Text;
}

void TextComponent::Awake()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->RegisterCommand(this);
	}

	// 레지스트리 등록은 수명의 시작(Awake)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterTextComponent(this);
}

void TextComponent::Update(float tick)
{
    const float currentZ = pos.z;

    isStretchX = false;
    isStretchY = false;
    stretchSize = { 0.f, 0.f };

    Mathf::Vector2 topLeft{};
    Mathf::Vector2 size{};
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
        layoutScale = rect->GetLayoutScale();
    }

    if (hasLayout)
    {
        const float verticalCenter = topLeft.y + size.y * 0.5f;
        float horizontalPos = topLeft.x;

        if (horizontalAlignment == TextAlignment::Center)
        {
            horizontalPos += size.x * 0.5f;
        }

        pos = { horizontalPos, verticalCenter, currentZ };
        stretchSize = size;
        isStretchX = size.x > 0.f;
        isStretchY = size.y > 0.f;
    }
    //pos += relpos;

    auto  image = GetOwner()->GetComponent<ImageComponent>();
    if (image)
        _layerorder = image->GetLayerOrder();
}

void TextComponent::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->UnregisterCommand(this);
	}

	// 해제는 무조건 한다. 예전에는 씬이 널이면 건너뛰어 레지스트리에 dangling이 남았다.
	UIManagers->UnregisterTextComponent(this);

	// 소속 캔버스 목록에서도 빠진다 — Canvas::Update의 매 프레임 청소를 대체한다(6-4).
	if (Canvas* owner = GetOwnerCanvas())
	{
		owner->RemoveUIObject(GetOwner());
	}
}

void TextComponent::SetFont(const file::path& path)
{
	file::path filepath = PathFinder::Relative("Font\\") / path.filename();
	auto _font = DataSystems->LoadSFont(filepath.wstring().c_str());
	font = _font;
	fontPath = path.filename().string();
}



