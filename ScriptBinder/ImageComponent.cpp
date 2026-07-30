#include "ImageComponent.h"
#include "Canvas.h"
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
	origin = { uiinfo.size.x * 0.5f, uiinfo.size.y * 0.5f };
}

void ImageComponent::ResetSize()
{
	if (m_curtexture)
	{
		uiinfo.size = m_curtexture->GetImageSize();
		origin = { uiinfo.size.x * 0.5f, uiinfo.size.y * 0.5f };
		if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
		{
			rect->SetSizeDelta(uiinfo.size);
		}
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

	// 레지스트리 등록은 수명의 시작(Awake)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterImageComponent(this);

	if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
	{
		const auto& worldRect = rect->GetWorldRect();
		const auto& pivot = rect->GetPivot();

		pos = { worldRect.x + worldRect.width * 2 * pivot.x,
				worldRect.y + worldRect.height * 2 * pivot.y,
				0.0f };
		scale = { worldRect.width / uiinfo.size.x,
				  worldRect.height / uiinfo.size.y };

		origin = { uiinfo.size.x * 0.5f,
				   uiinfo.size.y * 0.5f };

		scale *= unionScale;

		rect->SetSizeDelta(uiinfo.size);
	}
}

void ImageComponent::Update(float tick)
{
	if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
	{
		const auto& worldRect = rect->GetWorldRect();
		const auto& pivot = rect->GetPivot();

		pos = { worldRect.x + worldRect.width * 2 * pivot.x,
				worldRect.y + worldRect.height * 2 * pivot.y,
				0.0f };
		scale = { worldRect.width / uiinfo.size.x,
				  worldRect.height / uiinfo.size.y };

		origin = { uiinfo.size.x * 0.5f,
				   uiinfo.size.y * 0.5f };

		scale *= unionScale;

		//rect->SetSizeDelta(uiinfo.size);
	}
}

void ImageComponent::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->UnregisterCommand(this);
	}

	// 해제는 무조건 한다. 예전에는 씬이 널이면 건너뛰어 레지스트리에 dangling이 남았다.
	UIManagers->UnregisterImageComponent(this);

	// 소속 캔버스 목록에서도 빠진다 — Canvas::Update의 매 프레임 청소를 대체한다(6-4).
	if (Canvas* owner = GetOwnerCanvas())
	{
		owner->RemoveUIObject(GetOwner());
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



