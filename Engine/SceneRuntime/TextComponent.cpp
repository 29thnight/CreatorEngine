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
		renderScene->RegisterCommand(this);
	}

	// 레지스트리 등록은 수명의 시작(Awake)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterTextComponent(this);
}

void TextComponent::TickLayout(float tick)
{
    const float currentZ = pos.z;

    isStretchX = false;
    isStretchY = false;
    stretchSize = { 0.f, 0.f };

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

void TextComponent::OnUninitializing()
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

// 트랙 C3(레인 2: UI계) — UITickSystem 등록/해지. Awake/OnDestroy(컴포넌트당
// 1회 게이트)가 아니라 씬 편입/이탈 훅을 쓰는 이유는 UITickSystem.h 상단
// 주석 참조 — DDOL 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다.
// 실제 파괴 경로(PrefabUtility::ApplyComponentDiff·Scene::FlushPendingDestroy)
// 도 OnDestroy 직전에 OnRemovingFromScene을 먼저 부르므로, 이 시스템에서
// 빠지는 시점이 항상 실 파괴보다 먼저다.
void TextComponent::OnAddedToScene()
{
	UIComponent::OnAddedToScene();
	UITickSystems->RegisterText(this);
}

void TextComponent::OnRemovingFromScene()
{
	UITickSystems->UnregisterText(this);
	UIComponent::OnRemovingFromScene();
}

void TextComponent::SetFont(const file::path& path)
{
	// ★ 폰트 경로만 든다 (D4, 2026-08-09).
	//
	//   예전에는 여기서 DataSystem::LoadSFont로 DirectXTK SpriteFont를
	//   만들었다. 그것은 ID3D11Device를 요구하는 DX11 타입이고, 만든 뒤에
	//   그 포인터를 프록시까지 날랐지만 그리는 쪽은 T6에서 사라졌다 -
	//   즉 아무도 읽지 않는 포인터를 위해 DX11 디바이스를 붙들고 있었다.
	//
	//   ★ 게다가 읽을 자산도 없었다. Assets/Font/ 가 비어 있어 LoadSFont는
	//     존재하지 않는 파일로 SpriteFont를 만들려 하고 있었다.
	//
	//   경로는 저작 데이터라 그대로 둔다 - 앞으로 세울 SDF 폰트 계통이
	//   이 경로에서 아틀라스를 만든다.
	fontPath = path.filename().string();
}





void TextComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동. navigations 수동 복원 루프는
	// 이식하지 않는다 — typed 역직렬화가 반영 멤버를 채우므로 그 루프는
	// 이중 적재 버그였다(레거시 벡터 경로의 침묵 실패가 가리던 자리).
	std::string path = GetFontPath();
	if (!path.empty())
	{
		SetFont(path);
	}
	else
	{
		Debug->LogError("Text Component is missing font path");
	}
}

