#pragma once
#include "ClassProperty.h"
#include <vector>
#include <functional>

class CameraComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 렌더 — CameraComponent 가상 Update
// 오버라이드의 시스템 이관. 패턴은 AnimatorSystem과 동형이다 — 등록/해지 훅
// 선택 근거(왜 Awake/OnDestroy가 아니라 OnAddedToScene/OnRemovingFromScene인가,
// DDOL이 왜 문제인가)와 파괴 경로 전수 확인(FlushPendingDestroy·
// DetachEntityHierarchy·PrefabUtility::ApplyComponentDiff가 전부
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
// 상호배타, Entity.h·커밋 3c3aefb6 참고)부터 일부 GameObject(UI 계열)는
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
//
// ── CameraComponent 고유 사정 3: Update가 순회 중 재진입 시험의 발화점이다(트랙 C·C2-0) ──
//
// SceneGraphRedesignPlan 트랙 C의 C2-0 — 재진입 스트레스 시험(Scene::ArmReentrancyStress로
// 무장하고, Scene::FireReentrancyStress(true)가 "순회 중" 조건으로 파괴·생성을
// 일으킨다)이 C3로 RegistryTick이 사라지면서 발화 자리를 잃었다. 이 시스템의
// Update가 회귀 씬 4종 전부에서 비어 있지 않고(GetCount()==1), 루프 본문이 렌더
// 커맨드 같은 외부 부작용 없는 순수 필드 대입이라 재진입 신호가 섞이지 않는다는
// 이유로 그 발화 자리를 되찾았다(감사 근거는 Scene.h의 TryFireReentrancyStressMidTraversal
// 상단 주석 참고).
//
// midTraversalProbe는 그 발화점을 심는 콜백이다 — Scene::Update가
// `[this]{ TryFireReentrancyStressMidTraversal("CameraSystem", "Update"); }`를
// 넘긴다. 이 시스템이 Scene.h를 직접 include하지 않는 것은 의도다: 9종 시스템
// 전부가 지금 Scene에 의존하지 않는다는 것이 감사로 확인된 사실이고, 콜백
// 주입은 그 사실을 지키면서 발화점만 옮기는 방법이다. 비무장 상태의 비용은
// 콜백 안에서 bool 하나 읽는 것뿐이다(Scene::TryFireReentrancyStressMidTraversal
// 구현 참고) — 매 프레임 도는 핫패스이므로 그 이상을 여기서 하면 안 된다.
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
    //
    // midTraversalProbe: 순회 한복판 — 루프에 진입한 첫 반복에서 원소 유효성과
    // 무관하게 한 번 불리는 선택적 콜백이다(클래스 상단 "고유 사정 3", .cpp
    // 구현 참고). 비워 두면(기본값) 아무 일도 하지 않는다 — Scene 외 다른
    // 호출부(장차 생길 도구·테스트 등)가 이 시스템을 재진입 시험과 무관하게
    // 그냥 틱만 시키고 싶을 때를 위한 것이다.
    void Update(float tick, const std::function<void()>& midTraversalProbe = nullptr);

    size_t GetCount() const noexcept { return m_cameras.size(); }

private:
    std::vector<CameraComponent*> m_cameras;
};

static auto CameraSystems = CameraSystem::GetInstance();
