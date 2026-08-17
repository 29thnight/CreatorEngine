#include "UIComponent.h"
#include "GameObject.h"
#include "Canvas.h"
#include "UIManager.h"

float MaxOreder = 100.0f;

UIComponent::UIComponent()
{
}

void UIComponent::SetCanvas(Canvas* canvas)
{
	if (nullptr == canvas || nullptr == canvas->GetOwner())
	{
		m_ownerCanvasObject.reset();
		return;
	}

	m_ownerCanvasObject = canvas->GetOwner()->shared_from_this();

	// 직렬화용 이름은 연결 시점에 한 번 기록한다. 예전에는 Canvas::Update가
	// 매 프레임 이름 변화를 폴링해 자식들 문자열을 갱신했다(6-2에서 제거).
	// 런타임 연결은 이제 직접 참조라 이름이 어긋나도 동작에는 영향이 없고,
	// 이 값은 다음 로드 때 캔버스를 되찾는 열쇠로만 쓰인다.
	m_ownerCanvasName = canvas->GetOwner()->m_name.ToString();
}

Canvas* UIComponent::GetOwnerCanvas()
{
	auto canvasObject = m_ownerCanvasObject.lock();
	if (!canvasObject || canvasObject->IsDestroyMark()) return nullptr;

	return canvasObject->GetComponent<Canvas>();
}

void UIComponent::SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI)
{
    navigation[(int)dir] = otherUI;
    Navigation nav;
    nav.mode = (int)dir;
    nav.navObject = otherUI->GetInstanceID();

    auto it =  std::ranges::find_if(navigations, [&](const Navigation& n)
    {
        return n.mode == nav.mode; 
    });

    if (it == navigations.end())
    {
        navigations.push_back(nav);
    }
    else
    {
	    *it = nav;
    }
}

void UIComponent::DeserializeNavi()
{
    GameObject* thisObj = GetOwner();
	int navCount = 0;
    for (const auto& nav : navigations)
    {
        if (auto obj = thisObj->OwnerSceneFindInstanceID(nav.navObject))
        {
            navigation[(int)nav.mode] = obj->shared_from_this();
            ++navCount;
        }
	}

    if (navigations.size() == navCount)
    {
        isDeserialized = true;
    }
}

GameObject* UIComponent::GetNextNavi(Direction dir)
{
    if (auto next = navigation[(int)dir].lock())
		return next.get();

	return nullptr;
}

bool UIComponent::IsNavigationThis()
{
	GameObject* thisObj = GetOwner();
	auto selectedObj = UIManagers->SelectUI.lock();

    if (selectedObj && thisObj == selectedObj.get())
    {
        return true;
    }

    return false;
}


