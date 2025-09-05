#include "TextComponent.h"
#include "ImageComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RenderScene.h"
#include "UIManager.h"
#include "RectTransformComponent.h"

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
    if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
    {
            const auto& worldRect = rect->GetWorldRect();
            pos = { worldRect.x, worldRect.y };
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
