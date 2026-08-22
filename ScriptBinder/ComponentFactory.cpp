#include "ComponentFactory.h"
#include "LifecycleRegistry.h"
#include "Entity.h"
#include "Transform.h"
#include "RectTransformComponent.h"
#include "RenderableComponents.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "DataSystem.h"
#include "AnimationController.h"
#include "BoxColliderComponent.h"
#include "CharacterControllerComponent.h"
#include "Terrain.h"
#include "Model.h"
#include "NodeEditor.h"
#include "InvalidScriptComponent.h"
#include "ScriptComponent.h"
#include "FoliageComponent.h"
#include "BehaviorTreeComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "SoundComponent.h"
#include "PlayerInput.h"
#include "Animation.h"
#include "Canvas.h"
#include "UIManager.h"
	
void ComponentFactory::Initialize()
{
   // 생명주기 마스크 표를 먼저 세운다(PHASE 9-1).
   //
   // 이 자리인 이유: 컴포넌트 타입을 다루는 초기화가 여기 모여 있고, 첫 씬이
   // 로드되기 전이라 RegisterComponent가 빈 표를 만나는 일이 없다.
   Lifecycle::Registry::RegisterAllComponents();

   auto& registerMap = Meta::MetaDataRegistry->nameIndex; // CT11-b: 키는 정본 Type::name의 view

   for (const auto& [name, type] : registerMap)
   {
	   if (name == "Component" ||
		   name == "ScriptComponent" ||
		   name == "InvalidScriptComponent")
	   {
		   continue; // Skip base Component and ScriptComponent
	   }

	   // ── 손으로 붙이면 안 되는 컴포넌트 (트랙 S·S3 잔여, 2026-08-19) ──
	   //
	   // 아래 이름 규칙은 "Component"라는 **부분 문자열**로 거르기 때문에, 소유자가
	   // 따로 있는 컴포넌트까지 Add-Component 목록에 실어 버린다. 그 둘을 여기서
	   // 명시로 뺀다 — 규칙을 정교하게 만드는 대신 예외를 이름으로 박는 이유는,
	   // 이 목록이 이미 이름 기반이고 예외가 소수라 그 편이 읽기 쉬워서다.
	   //
	   //   · RectTransformComponent — 공간 컴포넌트의 부착은 GameObject::
	   //     AttachSpatialComponent가 단독으로 정한다(UI는 rect만, Canvas는 rect와
	   //     Transform 둘 다, 나머지는 Transform만). 목록에 실리면 3D 오브젝트에
	   //     손으로 붙일 수 있는데, AddComponent의 중복 검사는 **같은 타입만** 보므로
	   //     막지 못한다. 그러면 Scene::UpdateUILayout이 씬 루트부터 전체 트리를
	   //     훑으며 "RectTransform을 가졌는가"만으로 UI를 가르기 때문에, 그 3D
	   //     오브젝트가 매 프레임 UI 레이아웃 순회에 끼어들어 자식에게 스크린
	   //     좌표계를 전파한다 — 로그도 에러도 없이. (Transform은 이 이름 규칙 다섯
	   //     개 중 어디에도 안 걸려 원래부터 빠져 있다 — 아래 주석 참고.)
	   //
	   //   · BoneComponent — 뼈 마커의 부착은 ModelSceneBridge의 생성 경로와
	   //     SceneManager의 구파일 승격이 정한다(트랙 E·E7-b). 임의 오브젝트에
	   //     붙이면 순회의 뼈 분기를 타고 들어가 이름으로 뼈를 찾다 실패하고
	   //     로컬 행렬 폴백으로 빠진다 — 크래시는 아니지만 의미 없는 상태다.
	   if (name == "RectTransformComponent" || name == "BoneComponent")
	   {
		   continue;
	   }

	   // S1-b: Transform이 Component로 승격됐지만 아래 이름 규칙("Component"/
	   // "Renderer"/"Animator"/"UIButton"/"Canvas" 부분 문자열)에는 걸리지 않아
	   // 에디터 Add-Component 목록에 실리지 않는다 — 의도적으로 그대로 둔다.
	   // 모든 GameObject는 생성자에서 이미 Transform을 자동 부착하므로
	   // (Entity.cpp) 수동 추가는 AddComponent(Meta::Type&)의 중복 검사에
	   // 걸려 항상 no-op이다(기존 인스턴스를 되돌려주고 경고 로그만 남긴다) —
	   // 목록에 넣어봐야 클릭할 수 있는 유효한 동작이 없다.

	   size_t pos = name.find("Component");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[std::string(name)] = type;
	   }
	   pos = name.find("Renderer");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[std::string(name)] = type;
	   }
	   pos = name.find("Animator");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[std::string(name)] = type;
	   }
	   pos = name.find("UIButton");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[std::string(name)] = type;
	   }
	   pos = name.find("Canvas");
	   if (pos != std::string::npos)
	   {
		   m_componentTypes[std::string(name)] = type;
	   }
   }
}

void ComponentFactory::LoadComponent(Entity* obj, const MetaYml::detail::iterator_value& itNode, bool isEditorToGame)
{
	if (itNode["ModuleBehavior"])
	{
		// C++ 스크립트 은퇴(9-4): 구 포맷의 ModuleBehavior 노드는 더는 복원할 수 없다.
		// 자리 표시 컴포넌트를 붙여 씬 로드는 계속한다 — C# 스크립트로 교체 대상.
		obj->AddComponent<InvalidScriptComponent>();
		return;
	}

    const Meta::Type* componentType = Meta::ExtractTypeFromYAML(itNode);
    if (nullptr == componentType)
    {
        // K1-b: UUID도 이름+typeID도 못 맞히면 컴포넌트 노드 하나가 조용히
        // 사라진다 — §1.1의 "리네임 시 컴포넌트 소실"과 같은 증상이다. 로그
        // 없이 return하던 자리라 어떤 오브젝트에서 어떤 노드가 버려졌는지
        // 남긴다.
        Debug->LogError("ComponentFactory::LoadComponent: 컴포넌트 타입을 확정하지 못해 노드를 버림 - GameObject \""
            + obj->GetHashedName().ToString() + "\"");
        return;
    }

    // ScriptComponent만 한 오브젝트에 여럿 붙을 수 있다.
    //
    // AddComponent는 타입이 겹치면 새로 만들지 않고 기존 것을 돌려주므로, 스크립트를
    // 둘 이상 붙인 오브젝트는 씬을 다시 읽을 때 두 번째부터 통째로 사라졌다.
    // 재생을 누르면 씬을 직렬화해 사본을 만들기 때문에 여기서 바로 드러난다.
    const bool allowMultiple = (componentType->typeID == type_guid(ScriptComponent));

    // K2 스테이지 A: AddComponent/AddComponentAllowMultiple가 이제 raw
    // Component*를 직접 돌려준다 — .get()이 더는 필요 없다.
    //
	// S1-b/E7-c: 정식 Scene 생성 경로는 생성 아키타입에 맞는 공간 컴포넌트를
	// 먼저 붙인다(UI=Rect, Canvas=Rect+Transform, 나머지=Transform). 저장된
	// YAML의 공간 노드는 보통 새 컴포넌트가 아니라 기존 인스턴스의 필드 채우기다.
    // 그대로 AddComponent(Meta::Type&)를 타면 내부 중복 검사에 걸려 "이미
    // 존재" 경고가 씬을 로드할 때마다(오브젝트 수만큼) 로그에 쌓인다 — 여기서
    // 기존 인스턴스를 바로 찾아 그 소음을 피한다. 이후 Deserialize·SetOwner·
    // postLoad 흐름은 다른 컴포넌트와 완전히 동일하다.
	Component* component = nullptr;
	if (componentType->typeID == type_guid(Transform)
		|| componentType->typeID == type_guid(RectTransformComponent))
	{
		// E7-c: 생성 아키타입이 먼저 붙인 공간 컴포넌트에는 저장값만 채운다.
		// 리플렉션 기본 생성 클론처럼 아직 없다면 여기서 실제 타입을 만든다.
		component = obj->GetComponent(*componentType);
		if (!component)
			component = obj->AddComponent(*componentType);
	}
	else
	{
		component = allowMultiple
			? obj->AddComponentAllowMultiple(*componentType)
			: obj->AddComponent(*componentType);
	}

    if (component)
    {
        using namespace TypeTrait;
		HashedGuid typeID = componentType->typeID;
		// CT6-d: 타입별 하드코딩 분기 17개 소멸 — 역직렬화는 typed 디스패치,
		// 애셋 해석 등 후처리는 각 컴포넌트의 OnDeserialized 훅(TypeOps.postLoad)으로.
		// navigations·localRolloffCurve 수동 복원 루프는 미이식: 반영 멤버라 typed가
		// 채우며, 그 루프는 레거시 벡터 경로의 침묵 실패(MakeAnyFromRaw에 vector
		// 캐스터 부재)가 가리던 이중 적재였다.
		Meta::Deserialize(reinterpret_cast<void*>(component), *componentType, itNode);
		component->SetOwner(obj);

		// U7: typed 역직렬화는 새 계층 로컬 경로만 읽는다. 예전 navObject
		// (instanceID)는 UIComponent가 읽기 전용 승격 캐시에 받아 두었다가,
		// UIManager의 지연 연결에서 새 경로로 치유한다.
		if (auto* ui = dynamic_cast<UIComponent*>(component))
		{
			ui->LoadLegacyNavigation(itNode);
		}

		if (const Meta::Typed::TypeOps* ops = Meta::Typed::FindTypeOps(componentType->typeID.m_ID_Data))
		{
			if (nullptr != ops->postLoad)
			{
				ops->postLoad(component, itNode);
			}
		}

		if (isEditorToGame)
		{
			component->MakeInstanceID();
		}
		// Initialize if the component is initializable
		if (auto initializable = dynamic_cast<System::IInitializable*>(component))
		{
			initializable->Initialize();
		}
    }
}
