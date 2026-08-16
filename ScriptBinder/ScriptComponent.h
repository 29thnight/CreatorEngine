#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "Component.h"

// C# 스크립트 하나를 대표하는 네이티브 컴포넌트.
//
// 이것이 있어야 관리 스크립트가 기존 컴포넌트 체계에 편입된다 —
// 인스펙터 순회, YAML 직렬화, 프리팹 복제가 전부 따라온다.
// (그전에는 ClrHost가 GameObject→인스턴스 맵을 따로 들고 인스펙터도 별도 섹션을 그렸다)
//
// 주의: Update/LateUpdate/FixedUpdate를 override하지 않는다.
// RegistableEvent는 override한 메서드만 씬 이벤트에 등록하는데, 관리 측 라이프사이클은
// ClrHost가 틱당 한 번 일괄 디스패치한다(설계 문서 02절). 여기서 컴포넌트마다 등록하면
// 경계를 스크립트 수만큼 넘게 되어 그 설계가 무너진다.
// Awake/OnDestroy만 받는 이유는 인스턴스의 생성·파괴 시점을 잡기 위해서다.
class ScriptComponent : public Component
{
public:
   static consteval auto describe()
   {
       return meta::describe<ScriptComponent>(
           meta::base<Component>(),
           meta::member<&ScriptComponent::m_scriptType>(),
           meta::member<&ScriptComponent::m_fieldData>());
   }
	GENERATED_BODY(ScriptComponent)

	void Awake() override;
	void OnDestroy() override;

	// 붙일 C# 타입 이름. 이 값이 직렬화되어 씬·프리팹에 남고,
	// 로드 시 Awake에서 다시 인스턴스를 만든다.
	std::string m_scriptType{};

	// 노출 필드 값. 관리 객체 안에 있는 값이라 리플렉션이 직접 볼 수 없으므로,
	// 저장 시점에 여기로 옮겨 담고 로드 후 다시 밀어 넣는다.
	//
	// 형식은 "타입|값|이름". 이름을 마지막에 두는 이유는 표시 이름에 구분자가
	// 섞여 들어와도 앞의 두 칸만 끊어 읽으면 되기 때문이다.
	std::vector<std::string> m_fieldData{};

	// 관리 인스턴스의 현재 값을 m_fieldData로 옮겨 담는다(저장 직전·인스펙터 편집 후).
	void CaptureFields();

	// 핫리로드 직전에 부른다. 현재 값을 붙들어 두고 인스턴스 참조를 끊는다.
	//
	// 리로드는 스크립트 어셈블리를 통째로 내리므로 살아 있던 인스턴스가 전부 사라진다.
	// 여기서 값을 챙겨 두면 리로드 후 Awake가 같은 값으로 되살린다 —
	// 편집 중이던 상태가 유지되는 것이 핫리로드의 목적이다.
	void PrepareForReload();

	// 관리 인스턴스를 접어 둔다. 값은 m_fieldData에 남으므로 다음 Awake가 그대로 되살린다.
	//
	// 재생을 시작하면 엔진이 에디터 씬을 직렬화해 PlayScene을 따로 만든다(사본 방식).
	// 원본 씬은 파괴되지 않고 그대로 살아 있어서, 원본의 관리 인스턴스를 두면
	// 사본의 인스턴스와 함께 둘 다 틱을 받아 게임 로직이 두 벌 돈다.
	// PrepareForReload와 달리 관리 측이 내려가지 않으므로 파괴를 직접 요청한다.
	void SuspendInstance();

	// m_fieldData를 관리 인스턴스에 밀어 넣는다(인스턴스 생성 직후).
	void ApplyFields();

	// 관리 인스턴스 id. 런타임 전용이라 직렬화하지 않는다.
	int GetInstanceId() const { return m_instanceId; }

	// 씬 로드/복제로 만들어진 컴포넌트가 아직 인스턴스를 갖지 않았을 때 쓴다.
	bool HasInstance() const { return m_instanceId >= 0; }

private:
	int m_instanceId{ -1 };
};
#endif // !DYNAMICCPP_EXPORTS
