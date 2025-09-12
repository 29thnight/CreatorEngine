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
    if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
    {
            const auto& worldRect = rect->GetWorldRect();
            pos = { worldRect.x, worldRect.y };
            stretchSize = { worldRect.w, worldRect.h };

            if (GameObject::IsValidIndex(m_pOwner->m_parentIndex))
            {
                    if (auto* parent = GameObject::FindIndex(m_pOwner->m_parentIndex))
                    {
                            if (auto* parentRect = parent->GetComponent<RectTransformComponent>())
                            {
                                    const auto& pRect = parentRect->GetWorldRect();
                                    stretchSize = { pRect.w, pRect.h };
                            }
                    }
            }

            isStretchX = true;
            isStretchY = true;
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
