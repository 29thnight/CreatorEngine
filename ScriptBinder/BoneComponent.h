#pragma once
#include "Component.h"

// 저장된 GameObjectType::Bone 판정을 컴포넌트 질의로 옮기는 마커(트랙 E,
// E7-b). Scene::UpdateModelRecursive의 Bone 분기가 "이 오브젝트가 뼈인가"를
// 더 이상 저장된 enum이 아니라 이 컴포넌트의 보유 여부(HasComponent<BoneComponent>())로
// 판정한다. m_gameObjectType 필드 자체는 이번에 지우지 않는다(E7-c 소관).
//
// ── reflect()가 로컬 필드를 하나도 안 두는 이유 ──
//
// 뼈 인덱스는 Skeleton::m_bones 안에서의 위치이고, 이 값은 모델(스켈레톤
// 애셋)이 정하는 파생값이다. 저장해 두면 모델을 바꿔 끼운 뒤 죽은 인덱스로
// 애니메이션이 엉뚱한 뼈를 움직이거나 범위를 벗어나 읽는다 — 그래서 이
// 컴포넌트는 직렬화할 로컬 필드가 없다(schema<Self>()가 빈 로컬 스키마를
// 돌려주고, 기반 Component::m_FileID만 상속 질의로 계속 나온다).
//
// ── m_boneIndex·m_resolvedFor는 반대로 직렬화하면 안 되는 런타임 캐시다 ──
//
// Skeleton::FindBone(문자열 선형 탐색, RenderEngine/Skeleton.cpp)을 매 프레임
// 다시 돌지 않으려고 여기 담아 둔다. 저작 자산에 Bone 노드가 744개
// (Test1.creator 61 · 플레이어 프리팹마다 ~54)이고 Scene::UpdateModelRecursive
// 순회가 프레임당 3회 도므로, 이 캐시가 곧 성능 축이다.
//
// 캐시 적중 조건은 이 컴포넌트가 스스로 판단하지 않는다 — Scene.cpp의 Bone
// 분기가 m_resolvedSerial을 지금 애니메이터의 m_Skeleton->m_serial과 비교하고
// m_boneIndex가 유효 범위인지 확인한 뒤에만 쓴다. 스켈레톤이 늦게 붙거나(늦은
// 로드) 모델을 갈아 끼워 바뀌면 그 비교가 자동으로 깨져 다시 FindBone으로
// 푼다 — 옛 코드가 매 프레임 재탐색으로 공짜로 얻던 그 관용을 그대로
// 보존한다(Scene.cpp 주석 참고).
class BoneComponent : public meta::identity<BoneComponent, Component>
{
   public:
   // ★ 필드 0 · 메서드 1인 이유 — 둘 다 강제된 결과다.
   //
   //   필드가 0인 것은 위에 적은 대로 뼈 인덱스가 파생값이라 저장하면 안 되기
   //   때문이고, 그렇다고 스키마를 통째로 비울 수는 없다: meta::adapt<T>()가
   //   `field_count > 0 || method_count > 0`을 static_assert로 막는다
   //   (Utility_Framework/ReflectionMeta.h:105 — "빈 서술은 지원하지 않는다").
   //   Meta::Register<BoneComponent>()가 그 adapt를 타므로, 등록하는 순간
   //   빈 스키마는 컴파일 자체가 안 된다.
   //
   //   그래서 UIButton·SoundComponent와 같은 MethodOnly 형태를 쓴다 — 메서드만
   //   싣고 필드는 0. 직렬화 노드에는 타입 태그만 남고 값은 실리지 않는다.
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::method<&Self::GetResolvedBoneIndex>);
   }
public:
    BoneComponent() = default;
    virtual ~BoneComponent() = default;

    // 지금 캐시에 담긴 뼈 인덱스(-1이면 아직 못 풀었음). 진단용이자 위 reflect()가
    // 요구하는 최소 한 개의 서술 항목이다.
    int GetResolvedBoneIndex() { return m_boneIndex; }

    // 마지막으로 FindBone이 성공한 결과. -1이면 아직 못 풀었다는 뜻이고,
    // Scene.cpp는 이 값이 음수이거나 m_resolvedSerial이 어긋나면 무조건 다시 푼다.
    int m_boneIndex{ -1 };

    // m_boneIndex를 어느 스켈레톤에 대해 풀었는지 — Skeleton::m_serial 값이다.
    // 0은 "아직 아무 스켈레톤도 아님"(실제 스켈레톤의 일련번호는 1부터).
    //
    // ★ Skeleton* 포인터를 안 들고 일련번호를 드는 이유: 포인터는 해제된
    // 스켈레톤의 주소를 계속 들고 있게 되고(비교만 해도 저장 자체가 위험 신호),
    // 새 스켈레톤이 같은 주소를 재할당받으면 캐시가 우연히 적중해 다른 모델의
    // 뼈 인덱스를 조용히 재사용한다. 일련번호는 그 ABA를 원천 차단한다
    // (Skeleton.h의 m_serial 주석 참고).
    uint64 m_resolvedSerial{ 0 };
};
