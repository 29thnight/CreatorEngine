#pragma once
#include "ClassProperty.h"
#include <vector>

class FoliageComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — FoliageComponent 가상 Update
// 오버라이드의 시스템 이관. 패턴은 AnimatorSystem과 동형이다 — 등록/해지 훅
// 선택 근거와 파괴 경로 전수 확인은 AnimatorSystem.h 상단 주석 참고, 여기서는
// 반복하지 않는다.
//
// ── FoliageComponent 고유 사정(카메라 의존) ──
//
// 옛 FoliageComponent::Update는 컴포넌트마다 전역 활성 카메라
// 와 SceneManagers->GetRenderScene()을 다시 읽었다 — 둘 다 한 프레임 안에서는
// 바뀌지 않는 전역 조회라, 씬에 FoliageComponent가 N개면 N번 반복 조회하던
// 셈이다. 이 시스템의 Update는 두 조회를 루프 시작 전에 딱 한 번만 하고
// 모든 FoliageComponent가 그 결과를 공유한다 — 컴포넌트 수가 늘수록
// 이관의 실익이 커지는 지점이다.
//
// Awake/OnDestroy는 손대지 않는다 — FoliageComponent가 거기서 하는 일
// (scene->CollectFoliageComponent/UnCollectFoliageComponent,
// renderScene->RegisterCommand/UnregisterCommand)은 이 트랙의 대상인
// "Update 오버라이드"와 무관한 별도 축(렌더 등록)이다.
class FoliageSystem : public Singleton<FoliageSystem>
{
    friend class Singleton<FoliageSystem>;
    FoliageSystem() = default;
    ~FoliageSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem::Register와 같은 이유.
    void Register(FoliageComponent* foliage);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(FoliageComponent* foliage);

    // 등록된 FoliageComponent 전부를 한 번에 틱한다. 옛 Scene::RegistryTick이
    // 공통으로 해주던 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이
    // 대신 적용한다. 카메라·RenderScene 조회는 루프 밖에서 1회뿐이다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_foliages.size(); }

private:
    std::vector<FoliageComponent*> m_foliages;
};

static auto FoliageSystems = FoliageSystem::GetInstance();
