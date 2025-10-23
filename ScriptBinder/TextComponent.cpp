#include "TextComponent.h"
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
		UIManagers->UnregisterTextComponent(this);
	}
}

void TextComponent::SetFont(const file::path& path)
{
	file::path filepath = PathFinder::Relative("Font\\") / path.filename();
	auto _font = DataSystems->LoadSFont(filepath.wstring().c_str());
	font = _font;
	fontPath = path.filename().string();
}
