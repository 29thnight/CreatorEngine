#pragma once
#include "Component.h"
#include "EntityHandle.h"
#include <mathematics/matrix4x4.hpp>

// 저장된 GameObjectType::Bone 판정을 컴포넌트 질의로 옮기는 마커(트랙 E,
// E7-b). Scene::UpdateModelRecursive의 Bone 분기가 "이 오브젝트가 뼈인가"를
// 저장된 enum이 아니라 이 컴포넌트의 보유 여부(HasComponent<BoneComponent>())로
// 판정한다. E7-c 이후 m_gameObjectType은 구파일 YAML 승격 입력으로만 남는다.
//
// ── 뼈 인덱스는 직렬화하지 않는다 ──
//
// 뼈 인덱스는 Skeleton::m_bones 안에서의 위치이고, 이 값은 모델(스켈레톤
// 애셋)이 정하는 파생값이다. 저장해 두면 모델을 바꿔 끼운 뒤 죽은 인덱스로
// 애니메이션이 엉뚱한 뼈를 움직이거나 범위를 벗어나 읽는다 — 그래서 이
// 컴포넌트는 관측 고정 설정 m_bPinned만 직렬화한다. 인덱스와 런타임 관측
// 상태는 다시 구할 수 있으므로 저장하지 않는다.
//
// ── m_boneIndex·m_resolvedSerial은 직렬화하면 안 되는 런타임 캐시다 ──
//
// Skeleton::FindBone(문자열 선형 탐색, RenderEngine/Skeleton.cpp)을 매 프레임
// 다시 돌지 않으려고 여기 담아 둔다. 저작 자산에 Bone 노드가 744개
// (Test1.creator 61 · 플레이어 프리팹마다 ~54)이고 Scene::UpdateModelRecursive
// 순회가 프레임당 3회 도므로, 이 캐시가 곧 성능 축이다.
//
// 캐시 적중 조건은 이 컴포넌트가 스스로 판단하지 않는다. X7 packed 경로는
// Animator binding이 m_resolvedSerial을 skeleton serial과 비교하고 topology/owner까지
// 함께 검증한다. 스켈레톤이 늦게 붙거나 갈아 끼워지면 binding pass에서 다시
// FindBone으로 풀며, -1도 그 serial의 유효한 negative 결과로 캐시한다. 따라서
// 존재하지 않는 본을 steady frame마다 재탐색하지 않는다. recursive A/B fallback만
// Scene.cpp의 같은 serial 규약으로 이 필드를 직접 읽는다.
class [[reflgen::reflect]] BoneComponent : public meta::identity<BoneComponent, Component>
{
public:
    BoneComponent() = default;
    virtual ~BoneComponent() = default;

    // 지금 캐시에 담긴 뼈 인덱스(-1이면 아직 못 풀었음). 진단용이다.
    [[reflgen::reflect, creator::read_only_in_inspector]]
    int GetResolvedBoneIndex() { return m_boneIndex; }

    [[nodiscard]] math::matrix4x4 GetWorldTransform() const;

    // Explicit gameplay observation survives scene save/load. Automatic
    // promotion is runtime-only and is reset when the component is recreated.
    // m_bPinned는 저작 설정이며, bone index/serial/owner와 자동 승격은
    // 현재 scene 및 skeleton에 종속된 런타임 캐시다(그래서 아래는 reflgen::ignore).
    bool m_bPinned{ false };

    [[reflgen::ignore]]
    bool m_runtimeObserved{ false };

    [[reflgen::ignore]]
    EntityHandle m_animatorOwner{};

    // 마지막 binding의 FindBone 결과. -1은 아직 안 풀렸거나 그 skeleton에 없는
    // 본이라는 뜻이다. m_resolvedSerial이 같으면 negative 결과도 다시 찾지 않는다.
    [[reflgen::ignore]]
    int m_boneIndex{ -1 };

    // m_boneIndex를 어느 스켈레톤에 대해 풀었는지 — Skeleton::m_serial 값이다.
    // 0은 "아직 아무 스켈레톤도 아님"(실제 스켈레톤의 일련번호는 1부터).
    //
    // ★ Skeleton* 포인터를 안 들고 일련번호를 드는 이유: 포인터는 해제된
    // 스켈레톤의 주소를 계속 들고 있게 되고(비교만 해도 저장 자체가 위험 신호),
    // 새 스켈레톤이 같은 주소를 재할당받으면 캐시가 우연히 적중해 다른 모델의
    // 뼈 인덱스를 조용히 재사용한다. 일련번호는 그 ABA를 원천 차단한다
    // (Skeleton.h의 m_serial 주석 참고).
    [[reflgen::ignore]]
    uint64 m_resolvedSerial{ 0 };
};
