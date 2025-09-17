#include "ImageComponent.h"
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
	if (index < 0 || index >= textures.size())
		return;

	curindex = index;
	m_curtexture = textures[curindex];
        uiinfo.size = textures[curindex]->GetImageSize();

        if (GameObject* owner = m_pOwner)
        {
                if (auto* rect = owner->GetComponent<RectTransformComponent>())
                {
                        const auto& pivot = rect->GetPivot();
                        origin = { uiinfo.size.x * pivot.x, uiinfo.size.y * pivot.y };
                }
                else
                {
                        origin = { uiinfo.size.x * 0.5f, uiinfo.size.y * 0.5f };
                }
        }
        else
        {
                origin = { uiinfo.size.x * 0.5f, uiinfo.size.y * 0.5f };
        }
}

bool ImageComponent::isThisTextureExist(std::string_view path) const
{
	for (const auto& p : texturePaths)
	{
		if (p == path)
			return true;
	}

	return false;
}

void ImageComponent::Load(const std::shared_ptr<Texture>& ptr)
{
	if (nullptr == ptr)
		return;

	textures.push_back(ptr);
	std::string filename = ptr->m_name + ptr->m_extension;
	texturePaths.push_back(filename);
	if (1 == textures.size())
	{
		SetTexture(0);
	}
}

void ImageComponent::DeserializeTexture(const std::shared_ptr<Texture>& ptr)
{
	if (nullptr == ptr)
		return;

	textures.push_back(ptr);
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
        const auto& pivot = rect->GetPivot();

        pos = { worldRect.x + worldRect.width * pivot.x,
                worldRect.y + worldRect.height * pivot.y,
                0.0f };
        scale = { worldRect.width / uiinfo.size.x,
                  worldRect.height / uiinfo.size.y };

        origin = { uiinfo.size.x * pivot.x,
                   uiinfo.size.y * pivot.y };

        scale *= unionScale;

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
