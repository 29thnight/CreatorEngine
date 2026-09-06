#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "ScriptLifecyclePhase.h"

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
class ScriptComponent : public meta::identity<ScriptComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_scriptType>,
           meta::field<&Self::m_fieldData>);
   }
public:
	ScriptComponent() = default;

	void OnInitialized() override;
	void OnUninitializing() override;

	// 6단계 전부를 가상으로 받아 관리 측에 전달한다(트랙 L · L3 완결).
	// 드라이버가 네이티브 하나여야 "관리 큐가 부르는 것"과 "네이티브가 부르는 것"이
	// 어긋날 수 없다 — 그 이원화가 DDOL 이송 신호가 스크립트에 닿지 않던 원인이었다.
	//
	// 뒤쪽 셋은 관리 측 TearDown과 겹칠 수 있다(고아 청소·어셈블리 리로드는 구동할
	// 네이티브 컴포넌트가 없어 관리 측 발화가 남아야 한다). 그 화해는 관리 측
	// '축소 전달됨' 상태가 맡는다 — 사유는 ScriptLifecyclePhase.h.
	void OnAddedToScene() override;
	void OnBeginSimulation() override;
	void OnEndSimulation() override;
	void OnRemovingFromScene() override;

	// 네이티브에서만 일어나는 사건을 관리 인스턴스에 흘린다(트랙 L · L3 잔여).
	//
	// 가상 훅으로 만들지 않은 이유: Scene::FlushPendingDestroy가 **모든 파괴에서**
	// OnEndSimulation/OnRemovingFromScene을 부르고 그 뒤 DestroyBehaviour가 관리 측
	// TearDown을 태워 같은 둘을 또 부른다 — 가상으로 받으면 파괴마다 이중 발화한다.
	// 그래서 지금은 이중이 구조적으로 불가능한 자리(DDOL 이송 경로) 두 곳만 이
	// 창구를 부른다. 자세한 사유와 최종형은 ScriptLifecyclePhase.h 상단에 있다.
	void NotifyManagedLifecycle(ScriptLifecyclePhase phase);

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
	// Native initialization entries, including rejected attempts; runtime diagnostics only.
	std::uint64_t InitializationAttempts() const { return m_initializationAttempts; }

private:
	int m_instanceId{ -1 };
	std::uint64_t m_initializationAttempts{};
};
