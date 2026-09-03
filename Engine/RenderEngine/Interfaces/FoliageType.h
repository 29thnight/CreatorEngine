#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include <cstdint>
#include <memory>
#include <string>

// 데이터 경계 헤더는 렌더 본체를 include하지 않는다. shared_ptr는 불완전 타입을
// 보관할 수 있으므로 Mesh·Material 정의 없이도 소유권 계약을 표현할 수 있다.
class Material;
namespace experiment { struct Material; } // I5-D5c4
namespace assets { class ModelAssetGeneration; } // PHASE 3.75 MBC8: typed 정본

struct FoliageType
{
   public:
   static consteval auto reflect()
   {
       using Self = FoliageType;
       return meta::schema<Self>(
           meta::field<&Self::m_castShadow>,
           meta::field<&Self::m_isShadowRecive>,
           meta::field<&Self::m_modelName>);
   }
    std::shared_ptr<Material> m_material{};
    // I5-D5c4(S2c-2c) — 재질의 저작 정본(비직렬화 런타임 필드). Foliage 자산은
    // 재질을 따로 저작하지 않고 모델 것을 그대로 쓰므로 정본도 같은 generation
    // 재질에서 온다(메시가 가리키는 MaterialId) — MeshRenderer처럼 씬 diff를 얹을
    // 표면이 없어 인스턴스가 아니라 base 값 그대로다.
    std::shared_ptr<const experiment::Material> m_authoredMaterial{};
    // PHASE 3.75 MBC8 — typed 정본(MeshRenderer m_modelGeneration 패턴). 비직렬화
    // 런타임 필드. FoliageComponent::BindExperimentMesh가 m_modelName → ModelId →
    // generation으로 잇고, 프록시 DrawSource와 drawPool이 RHIModelMeshView로 나른다
    // (experiment 핸들·legacy Mesh보다 먼저 소비된다). 재질의 embedded texture는
    // 같은 generation closure에서 푼다.
    std::shared_ptr<const assets::ModelAssetGeneration> m_modelGeneration{};
    std::uint32_t m_modelMeshIndex{ 0 };
    bool m_castShadow{ true };
    bool m_isShadowRecive{ true };
	std::string m_modelName{};

	FoliageType() = default;
	~FoliageType() = default;

    // MBC9 — 모델 이름이 유일한 영속 신원이다. 런타임 필드는
    // FoliageComponent::BindModelGeneration이 이름 → ModelId → generation으로 잇는다.
    explicit FoliageType(const std::string& modelName, bool castShadow = true)
        : m_castShadow(castShadow), m_modelName(modelName) {}
    bool operator==(const FoliageType& other) const
    {
        return m_modelName == other.m_modelName && m_castShadow == other.m_castShadow;
	}

};
