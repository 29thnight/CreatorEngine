#pragma once
#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector4.hpp>
#include <vector>
#include <cstdint>

// SceneGraphRedesignPlan.md §4 트랙 S, S1 — 계층·트랜스폼 파생 데이터의 유일한
// 정본(SoA).
//
// position/rotation/scale/m_parentID는 옮기지 않는다 — 실측 근거는
// Transform.h 상단 주석 참고(ScriptBinder.vcxproj의 PreBuildEvent
// `HeaderTool\MetaGenerator.exe`가 [[Property]] 태그를 물리 멤버의
// pointer-to-member로만 리플렉션하고, 그 결과물(Transform.generated.h)은 매
// 빌드 재생성되어 손수정이 무의미하다 — Utility_Framework/ReflectionFunction.h:277
// MakeProperty가 `T ClassT::* member`만 받는다). 여기 스토어는 어느 프로퍼티도
// 아닌, 매 프레임 파생되는 상태만 슬롯 인덱스와 평행하게 들고 있는다: 로컬/월드
// 행렬, dirty, 월드 캐시(스케일·쿼터니언·포지션).
//
// 슬롯은 Scene::AllocateSlot/ReleaseSlot과 1:1 동기다(Scene.cpp) — 별도
// 세대·프리리스트를 두지 않는다. 씬의 슬롯맵이 이미 수명을 관리하므로 이중
// 관리하지 않는다: AllocateSlot이 새 슬롯을 늘릴 때만 GrowOne()으로 뒤에
// 붙이고(프리리스트 재사용 슬롯은 ReleaseSlot이 이미 초기값으로 되돌려 놨다),
// ReleaseSlot이 ResetSlot()으로 다음 입주자를 위해 슬롯을 리셋한다.
//
// 저장 타입은 Mathematics의 packed 값 타입만 쓴다. matrix4x4/vector4는 각각
// 64/16바이트 standard-layout 값이라 슬롯 컨테이너에 저장할 수 있고, SIMD
// 레지스터 타입을 수명 경계 밖에 보관하지 않는다.
struct TransformStore
{
    std::vector<math::matrix4x4> localMatrix;
    std::vector<math::matrix4x4> worldMatrix;
    std::vector<uint8_t>        dirty;
    std::vector<math::vector4>   worldScale;
    std::vector<math::vector4>   worldQuaternion;
    std::vector<math::vector4>   worldPosition;

    // S2(dirty push / lazy pull) — dirty와는 독립된 두 번째 플래그. dirty는
    // "로컬 포즈(position/rotation/scale)가 재계산 대상"이라는 뜻이고 GetLocalMatrix/
    // UpdateLocalMatrix가 매 소비마다 지운다. worldChanged는 "SetAndDecomposeMatrix가
    // 마지막으로 순회가 소비한 이후 월드 행렬을 실제로 새로 썼다"는 뜻이고
    // Scene::UpdateModelRecursive만 소비(ConsumeWorldChanged)해서 내린다.
    //
    // 왜 따로 두나 — SetAndDecomposeMatrix를 부르는 경로가 Scene 순회 하나가
    // 아니다(ClrHost::EnsureWorldMatrix가 스크립트에서 즉시 읽기 위해 조상
    // 체인만 앞당겨 갱신하고, PhysicsManager도 물리 결과를 직접 반영한다).
    // 그런 순회 밖 갱신은 dirty를 이미 꺼버리므로, dirty만 보고 자식 전파 여부를
    // 정하면 그 자식(정확히는 갱신 안 된 형제 서브트리)이 갱신을 영영 놓친다 —
    // 값을 실제로 쓰는 단일 지점(SetAndDecomposeMatrix)에서 세우는 이 플래그가
    // 호출 경로와 무관하게 "자식에게 아직 못 알린 변경이 있다"를 보장한다.
    std::vector<uint8_t>        worldChanged;

    size_t Size() const { return localMatrix.size(); }

    // Scene::AllocateSlot 전용 — 슬롯 하나를 뒤에 늘린다.
    void GrowOne()
    {
        localMatrix.emplace_back(math::matrix4x4::identity());
        worldMatrix.emplace_back(math::matrix4x4::identity());
        dirty.push_back(1);
        worldScale.emplace_back(1.f, 1.f, 1.f, 1.f);
        worldQuaternion.emplace_back(0.f, 0.f, 0.f, 1.f);
        worldPosition.emplace_back(0.f, 0.f, 0.f, 1.f);
        // 새 슬롯은 첫 순회에서 반드시 자식에게 전파돼야 한다(dirty=1과 같은 이유).
        worldChanged.push_back(1);
    }

    // Scene::ReleaseSlot 전용 — 다음 입주자가 깨끗한 값을 보도록 슬롯을 리셋한다.
    void ResetSlot(size_t slot)
    {
        if (slot >= localMatrix.size()) return;
        localMatrix[slot] = math::matrix4x4::identity();
        worldMatrix[slot] = math::matrix4x4::identity();
        dirty[slot] = 1;
        worldScale[slot] = math::vector4{ 1.f, 1.f, 1.f, 1.f };
        worldQuaternion[slot] = math::vector4{ 0.f, 0.f, 0.f, 1.f };
        worldPosition[slot] = math::vector4{ 0.f, 0.f, 0.f, 1.f };
        worldChanged[slot] = 1;
    }
};
