#include "ImageComponent.h"
#include "../RenderEngine/DeviceState.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/mesh.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "transform.h"
#include "RectTransformComponent.h"
#include "UIManager.h"

ImageComponent::ImageComponent()
{
	m_name = "ImageComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<ImageComponent>();
	type = UItype::Image;
}

void ImageComponent::SetTexture(int index)
{
	curindex = index;
	m_curtexture = textures[curindex];
	uiinfo.size = textures[curindex]->GetImageSize();

	origin = { uiinfo.size.x / 2, uiinfo.size.y / 2 };
}

void ImageComponent::Load(const std::shared_ptr<Texture>& ptr)
{
	if (nullptr == ptr)
		return;

	textures.push_back(ptr);
	texturePaths.push_back(ptr->m_name);
	if (1 == textures.size())
	{
		SetTexture(0);
	}
}

void ImageComponent::Awake()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->RegisterCommand(this);
	}
}

void ImageComponent::Update(float tick)
{
    if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
    {
        const auto& worldRect = rect->GetWorldRect();
        pos = { worldRect.x + worldRect.width * 0.5f,
                worldRect.y + worldRect.height * 0.5f,
                0.0f };
        scale = { worldRect.width / uiinfo.size.x,
                  worldRect.height / uiinfo.size.y };

		rect->SetSizeDelta(uiinfo.size);
    }
}

void ImageComponent::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->UnregisterCommand(this);
		UIManagers->UnregisterImageComponent(this);
	}
}

void ImageComponent::UpdateTexture()
{
	if (curindex <= 0)
		curindex = 0;
	if (curindex >= textures.size())
		curindex = textures.size() - 1;

	SetTexture(curindex);
}
