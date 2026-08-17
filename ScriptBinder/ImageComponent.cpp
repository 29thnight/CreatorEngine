#include "ImageComponent.h"
#include "DataSystem.h"
#include "Canvas.h"
#include "ImageComponent.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.
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

void ImageComponent::SetNativeSize()
{
	if (!m_curtexture) return;

	uiinfo.size = m_curtexture->GetImageSize();
	origin = { uiinfo.size.x * 0.5f, uiinfo.size.y * 0.5f };

	// 크기가 0인 텍스처로 rect를 덮으면 요소가 사라진다. 그런 텍스처는 아직
	// 로드되지 않았거나 깨진 것이므로, 저작된 크기를 지우지 않고 그대로 둔다.
	if (uiinfo.size.x <= 0.f || uiinfo.size.y <= 0.f) return;

	if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
	{
		rect->SetSizeDelta(uiinfo.size);
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

	// 크기를 텍스처에 맡기기로 한 이미지만 여기서 한 번 맞춘다.
	// 예전에는 무조건 했고, 그래서 저작한 크기가 매번 지워졌다(분석 문서 F-7).
	if (useNativeTextureSize)
	{
		SetNativeSize();
	}

	RefreshTransformFromRect();
}

void ImageComponent::Update(float tick)
{
	RefreshTransformFromRect();
}

void ImageComponent::RefreshTransformFromRect()
{
	auto* rect = m_pOwner ? m_pOwner->GetComponent<RectTransformComponent>() : nullptr;
	if (nullptr == rect) return;

	const auto& worldRect = rect->GetWorldRect();

	// pos는 rect의 중심이다 — SpriteSheetComponent와 UIButton의 히트 테스트가 쓰는
	// 것과 같은 규약이다(PHASE 7-6).
	//
	// 예전 식은 worldRect.x + width * 2 * pivot.x 였다. pivot 0.5에서는 우연히 맞았지만
	// (2*0.5 = 1이라 아래 렌더 경로의 보정과 정확히 상쇄됐다) pivot이 다른 값이면
	// width*(2p-1) 만큼 어긋난다. worldRect는 이미 pivot을 반영해 계산된 결과이므로
	// 여기서 pivot을 또 곱하는 것은 이중 적용이다(분석 문서 F-8).
	// 현재 에셋의 pivot이 전부 0.5라 증상이 드러나지 않았을 뿐이다.
	pos = { worldRect.x + worldRect.width * 0.5f,
			worldRect.y + worldRect.height * 0.5f,
			0.0f };

	// 텍스처가 없거나 아직 로드되지 않으면 uiinfo.size가 0이다. 나누면 inf/NaN이
	// 렌더러까지 흘러가므로 그 프레임의 배율을 1로 둔다 — 예전에는 그대로 나눴다.
	scale = { uiinfo.size.x > 0.f ? worldRect.width / uiinfo.size.x : 1.f,
			  uiinfo.size.y > 0.f ? worldRect.height / uiinfo.size.y : 1.f };

	origin = { uiinfo.size.x * 0.5f,
			   uiinfo.size.y * 0.5f };

	scale *= unionScale;
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





void ImageComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동. 캔버스 연결은 UIManager::Update의
	// 일괄 연결로 미룬다(6-3). navigations 수동 복원은 이중 적재라 미이식.
	for (auto& imagePath : GetTexturePaths())
	{
		if (imagePath.empty())
			continue;

		auto texture = DataSystems->LoadSharedTexture(imagePath,
			DataSystem::TextureFileType::UITexture);
		if (!texture)
		{
			Debug->LogError("Failed to load texture for ImageComponent: " + imagePath);
			continue;
		}
		DeserializeTexture(texture);
	}
}

