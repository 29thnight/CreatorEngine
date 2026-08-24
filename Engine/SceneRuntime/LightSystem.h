#pragma once
#include "ClassProperty.h"
#include <vector>

class LightComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 렌더 — LightComponent 가상 Update
// 오버라이드의 시스템 이관. 패턴은 AnimatorSystem과 동형이다 — 등록/해지 훅
// 선택 근거와 파괴 경로 전수 확인은 AnimatorSystem.h 상단 주석에 이미 있다 —
// 여기서는 반복하지 않는다.
//
// ── LightComponent 고유 사정: 전용 진입점을 새로 만들지 않는다 ──
//
// 옛 LightComponent::Update 본문은 `renderScene->UpdateCommand(this)` 한 줄뿐
// 이다 — LightComponent의 사유 멤버를 건드리지 않고 this 포인터만 필요하다.
// 이 컴포넌트를 타입 포인터로 직접 틱하던 외부 호출부도 없다(전수 검색
// 확인). Animator/Decal/Foliage와 같은 이유로 LightComponent에 새 메서드를
// 추가하지 않고, 이 시스템의 Update가 옛 본문을 그대로 흡수한다.
//
// ★ RenderScene.h는 이 헤더가 include하지 않는다 — LightComponent.h 상단
//   주석이 이미 밝힌 저장소 규약이 이 시스템에도 그대로 적용된다(광원이 렌더
//   보유층에 등록되며 RenderScene.h가 필요해졌는데, 그 헤더가 Camera·
//   RenderPassData·프록시 계층을 통째로 끌고 온다 — ScriptBinder의 어느
//   헤더도 그것을 include하지 않는 것이 규약이다). 정의는 LightSystem.cpp에
//   있다.
class LightSystem : public Singleton<LightSystem>
{
    friend class Singleton<LightSystem>;
    LightSystem() = default;
    ~LightSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem::Register와 같은 이유.
    void Register(LightComponent* light);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(LightComponent* light);

    // 등록된 LightComponent 전부를 한 번에 틱한다. 옛 Scene::RegistryTick이
    // 공통으로 해주던 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이
    // 대신 적용한다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_lights.size(); }

private:
    std::vector<LightComponent*> m_lights;
};

static auto LightSystems = LightSystem::GetInstance();
