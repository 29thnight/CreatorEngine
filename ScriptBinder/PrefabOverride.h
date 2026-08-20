#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

// 프리팹 인스턴스가 프리팹 원본 값을 지역적으로 덮어쓴 지점 하나를 기록한다
// (SceneGraphRedesignPlan.md 트랙 P, P1 — 직렬화 예외 1).
//
// 예전에는 이 정보가 명시 데이터가 아니라 추론이었다 — GameObject::m_prefabOriginal
// (비직렬화 YAML 스냅샷)과 현재 값을 매번 YAML::Dump 문자열로 비교해 "다르면
// 오버라이드"를 매번 다시 계산했다(ReflectionYml.h의 옛 DeserializePrefab). 스냅샷이
// 비직렬화라 씬을 재로드하면 그 추론 근거 자체가 사라졌다. 이제 오버라이드를
// 직렬화되는 벡터로 직접 들고 다닌다 — 이 저장소의 리플렉션 직렬화기는 map을
// 지원하지 않으므로 등록 구조체 벡터로 표현한다(BTBuildGraph::NodeList가 std::vector
// <BTBuildNode>를 [[Property]]로 갖는 것과 같은 관용구). GameObject가
// std::vector<PrefabOverride>로 이 구조체를 붙인다.
//
// m_componentType이 비어 있으면 Entity 자신의 프로퍼티(예: m_tag)를 가리키고,
// 채워져 있으면 그 이름의 컴포넌트 타입에 속한 프로퍼티를 가리킨다.
struct PrefabOverride
{
    public:
    static consteval auto reflect()
    {
        using Self = PrefabOverride;
        return meta::schema<Self>(
            meta::field<&Self::m_componentType>,
            meta::field<&Self::m_componentSlot>,
            meta::field<&Self::m_propertyName>,
            meta::field<&Self::m_valueYaml>);
    }
    PrefabOverride() = default;

    std::string m_componentType{};

    // 같은 타입 컴포넌트가 여럿일 때 그중 **몇 번째**인지 (0-based).
    //
    // ── 왜 필요한가 (SceneGraphRedesignPlan P4-c) ──
    //
    // 이 필드가 없던 동안 오버라이드는 타입 이름 하나로 그 타입 **전체**를
    // 가리켰다. 저작 자산에 실제로 걸린다: GameManager.prefab이 ModuleBehavior를
    // 6개, MonsterAEvent/MonsterBEvent가 2개씩 얹는다. 그 인스턴스에서 스크립트
    // #3의 프로퍼티를 손보면 배제 집합이 6개 전부에 걸려, 프리팹 갱신이 나머지
    // 다섯에 새 값을 못 민다.
    //
    // ── -1의 뜻 ──
    //
    // "순번 미지정 = 그 타입 전체". 이 필드가 없던 시절에 적힌 데이터가
    // 역직렬화되면 기본값 -1이 되어 **예전 행동 그대로** 동작한다.
    // 새로 기록되는 것은 실제 순번을 담는다.
    //
    // ── 순번의 정의는 하나다 ──
    //
    // PrefabUtility::ComputeComponentSlot이 정본이다. 기록하는 쪽과 소비하는 쪽
    // (ApplyComponentDiff)이 서로 다른 규칙으로 세면 조용히 어긋나므로, 세는
    // 규칙을 그 함수 하나로 모았다.
    int m_componentSlot{ -1 };

    std::string m_propertyName{};

    // 오버라이드된 값의 YAML 직렬화 문자열. 되돌릴 때(또는 컴포넌트 재생성 뒤
    // 되먹일 때) 이 문자열을 다시 파싱해 그 프로퍼티 하나에만 적용한다.
    std::string m_valueYaml{};
};
