#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "PrsmScriptHost.h"

// PrSM 컴포넌트 하나를 대표하는 네이티브 컴포넌트 (ScriptComponent 의 PrSM 짝).
//
// 이것이 있어야 PrSM `class X : Component` 가 기존 컴포넌트 체계에 편입된다 — 인스펙터
// 순회, YAML 직렬화, 프리팹 복제가 전부 따라온다. 스크립트 인스턴스는 PrsmScriptHost 의
// GC 객체이고 여기에는 id 만 있다.
//
// 6단계 훅과 OnEnable/OnDisable 을 전부 받아 호스트로 넘긴다. 틱(PrePhysics/PostPhysics)은
// 여기서 받지 않는다 — 호스트가 프레임당 한 번 일괄로 돌린다(ClrHost 와 같은 규약).
// 재생 중에만 생명주기를 받는 관문은 Scene::DrainPendingPhases 에 있다(C# 스크립트와 같다).
class [[reflgen::reflect]] PrsmScriptComponent : public meta::identity<PrsmScriptComponent, Component>
{
public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

    PrsmScriptComponent() = default;

    void OnInitialized() override;
    void OnAddedToScene() override;
    void OnEnable() override;
    void OnBeginSimulation() override;
    void OnEndSimulation() override;
    void OnDisable() override;
    void OnRemovingFromScene() override;
    void OnUninitializing() override;

    // 붙일 PrSM 컴포넌트 타입의 정규 이름(예: "Mover", "Game::Mover").
    // 이 값이 씬·프리팹에 저장되고, 재생 시 OnInitialized 에서 인스턴스를 만든다.
    std::string m_typeName{};

    // @serialize 필드 값. 형식은 "타입id|정확한 비트(16진)|이름" — 부동소수의 NaN 페이로드와
    // 부호 있는 0 까지 비트로 보존한다(D-035). 인스턴스가 사라지기 전에 여기로 옮겨 둔다.
    std::vector<std::string> m_fieldData{};

    bool HasInstance() const { return PrsmScriptHost::Get().IsLive(m_instance); }

private:
    void Release();

    [[reflgen::ignore]]
    PrsmInstanceId m_instance{};
};
