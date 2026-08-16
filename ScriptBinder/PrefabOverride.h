#pragma once
#include "Core.Minimal.h"
#include "PrefabOverride.generated.h"

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
// m_componentType이 비어 있으면 GameObject 자신의 프로퍼티(예: m_tag)를 가리키고,
// 채워져 있으면 그 이름의 컴포넌트 타입에 속한 프로퍼티를 가리킨다.
struct PrefabOverride
{
    ReflectPrefabOverride
    [[Serializable]]
    PrefabOverride() = default;

    [[Property]]
    std::string m_componentType{};

    [[Property]]
    std::string m_propertyName{};

    // 오버라이드된 값의 YAML 직렬화 문자열. 되돌릴 때(또는 컴포넌트 재생성 뒤
    // 되먹일 때) 이 문자열을 다시 파싱해 그 프로퍼티 하나에만 적용한다.
    [[Property]]
    std::string m_valueYaml{};
};
