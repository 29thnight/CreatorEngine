#pragma once
#include "../Utility_Framework/Core.Mathf.h"
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
// Mathf::Matrix/Vector4(SimpleMath, XMFLOAT 기반)로 저장한다 — Mathf::xMatrix/
// xVector(XMMATRIX/XMVECTOR)는 16바이트 정렬 SIMD 레지스터 타입이라 컨테이너
// 저장에는 부적합하고, SimpleMath 쪽은 XM* 타입과 양방향 암시적 변환을
// 제공해(SimpleMath.h Matrix(CXMMATRIX)/operator XMMATRIX 등) 계산 지점에서만
// 레지스터로 올리면 된다.
struct TransformStore
{
    std::vector<Mathf::Matrix>  localMatrix;
    std::vector<Mathf::Matrix>  worldMatrix;
    std::vector<uint8_t>        dirty;
    std::vector<Mathf::Vector4> worldScale;
    std::vector<Mathf::Vector4> worldQuaternion;
    std::vector<Mathf::Vector4> worldPosition;

    size_t Size() const { return localMatrix.size(); }

    // Scene::AllocateSlot 전용 — 슬롯 하나를 뒤에 늘린다.
    void GrowOne()
    {
        localMatrix.emplace_back(Mathf::Matrix::Identity);
        worldMatrix.emplace_back(Mathf::Matrix::Identity);
        dirty.push_back(1);
        worldScale.emplace_back(1.f, 1.f, 1.f, 1.f);
        worldQuaternion.emplace_back(0.f, 0.f, 0.f, 1.f);
        worldPosition.emplace_back(0.f, 0.f, 0.f, 1.f);
    }

    // Scene::ReleaseSlot 전용 — 다음 입주자가 깨끗한 값을 보도록 슬롯을 리셋한다.
    void ResetSlot(size_t slot)
    {
        if (slot >= localMatrix.size()) return;
        localMatrix[slot] = Mathf::Matrix::Identity;
        worldMatrix[slot] = Mathf::Matrix::Identity;
        dirty[slot] = 1;
        worldScale[slot] = Mathf::Vector4(1.f, 1.f, 1.f, 1.f);
        worldQuaternion[slot] = Mathf::Vector4(0.f, 0.f, 0.f, 1.f);
        worldPosition[slot] = Mathf::Vector4(0.f, 0.f, 0.f, 1.f);
    }
};
