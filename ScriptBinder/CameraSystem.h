#pragma once
#include "ClassProperty.h"
#include <vector>

class CameraComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 렌더 — CameraComponent 가상 Update
// 오버라이드의 시스템 이관. 패턴은 AnimatorSystem과 동형이다 — 등록/해지 훅
// 선택 근거(왜 Awake/OnDestroy가 아니라 OnAddedToScene/OnRemovingFromScene인가,
// DDOL이 왜 문제인가)와 파괴 경로 전수 확인(FlushPendingDestroy·
// DetachGameObjectHierarchy·PrefabUtility::ApplyComponentDiff가 전부
// OnRemovingFromScene을 실 파괴보다 먼저 부른다는 근거)은 AnimatorSystem.h
// 상단 주석에 이미 있다 — 여기서는 반복하지 않는다.
//
// ── CameraComponent 고유 사정 1: 전용 진입점을 새로 만들지 않는다 ──
//
// Animator/Decal/Foliage와 같은 이유다 — 이 컴포넌트를 타입 포인터로 직접
// 틱하던 외부 호출부가 없다(전수 검색 확인 — 리포 전체에서
// `grep -rn -- "->Update("`가 Camera/Light에 매치되는 곳 0건, PlayerInputComponent
// ::TickInput이 필요했던 Scene::InternalPauseUpdateForUI 같은 우회로도 없다).
// 그래서 CameraComponent에 새 메서드(예: TickCamera)를 추가하지 않는다 —
// 이 시스템의 Update가 CameraComponent가 이미 공개해 둔 GetCamera()로 Camera
// 객체를 얻어 그 공개 필드(m_eyePosition·m_forward·m_up·m_right·m_lookAt,
// Camera.h에서 전부 public)를 직접 채운다. m_pCamera 자체는 CameraComponent의
// 사유 멤버로 그대로 남는다 — 캡슐화를 넓히지 않는다.
//
// ── CameraComponent 고유 사정 2: HasTransform() 방어를 추가하지 않는다 ──
//
// 옛 본문은 m_pOwner->Transform_()를 그대로 읽는다. S3(공간 컴포넌트
// 상호배타, GameObject.h·커밋 3c3aefb6 참고)부터 일부 GameObject(UI 계열)는
// Transform이 없고 Transform_()가 널 폴백(공유 더미 + 오브젝트당 1회 로그)을
// 돌려준다. 그런데 카메라는 뷰/렌더 파이프라인 전용 컴포넌트라 RectTransform
// 레이아웃과 결합할 설계상 이유가 없고, 저장소 어디에도 UI 계열 GameObject에
// CameraComponent를 붙인 프리팹이 없다(정적 판정이라는 한계는 인지한다 —
// diagnostic-with-transition 교훈대로 이 판정도 한 번 놓칠 수 있다). 그래서
// HasTransform() 조기 반환을 새로 추가하지 않고 옛 본문 그대로 옮긴다:
// 추가하면 "카메라가 실수로 UI에 붙었다"는 사건이 조용한 스킵(카메라가 그
// 프레임 그냥 갱신되지 않고 멈춘 채 남는다)이 되어, S3가 세운 진단 우선
// 철학(크래시 대신 로그로 잡는다)과 반대로 간다. 실제로 그런 프리팹이 생기면
// Transform_()의 폴백 로그가 그 사건을 그대로 지목한다 — 이 시스템이 따로
// 막으면 오히려 그 신호를 지운다.
class CameraSystem : public Singleton<CameraSystem>
{
    friend class Singleton<CameraSystem>;
    CameraSystem() = default;
    ~CameraSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem::Register와 같은 이유
    // (조밀 벡터가 매 프레임 실제 로직을 돌므로, 중복이 들어가면 같은
    // CameraComponent가 한 프레임에 두 번 갱신된다).
    void Register(CameraComponent* camera);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(CameraComponent* camera);

    // 등록된 CameraComponent 전부를 한 번에 틱한다. 옛 Scene::RegistryTick이
    // 공통으로 해주던 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이
    // 대신 적용한다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_cameras.size(); }

private:
    std::vector<CameraComponent*> m_cameras;
};

static auto CameraSystems = CameraSystem::GetInstance();
