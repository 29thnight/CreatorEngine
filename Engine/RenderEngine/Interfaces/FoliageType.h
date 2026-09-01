#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include <cstdint>
#include <memory>
#include <string>

// 데이터 경계 헤더는 렌더 본체를 include하지 않는다. shared_ptr는 불완전 타입을
// 보관할 수 있으므로 Mesh·Material 정의 없이도 소유권 계약을 표현할 수 있다.
class Mesh;
class Material;
namespace experiment { class Model; }
namespace experiment { struct Material; } // I5-D5c4 // I5-D5a: 메시 핸들 병행

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
    std::shared_ptr<Mesh> m_mesh{};
    std::shared_ptr<Material> m_material{};
    // I5-D5a — experiment 메시 핸들 병행(MeshRenderer m_experimentModel 패턴).
    // m_mesh와 같은 지위의 비직렬화 런타임 필드 — 바인딩은
    // FoliageComponent::BindExperimentMesh(신원 조회)가 잇고, 프록시 DrawSource와
    // 렌더 drawPool이 이것을 아이템 experimentView로 나른다(D4b 사슬 합류).
    std::shared_ptr<const experiment::Model> m_experimentModel{};
    std::uint32_t m_experimentMeshIndex{ 0 };
    // I5-D5c4(S2c-2c) — 재질의 experiment 저작 정본. 메시 핸들과 같은 지위의
    // 비직렬화 런타임 필드다. Foliage 자산은 재질을 따로 저작하지 않고 모델
    // 것을 그대로 쓰므로, 정본도 같은 experiment 모델에서 온다(메시가 가리키는
    // MaterialIndex) — MeshRenderer처럼 씬 diff를 얹을 표면이 없어 인스턴스가
    // 아니라 base 값 그대로다.
    std::shared_ptr<const experiment::Material> m_authoredMaterial{};
    bool m_castShadow{ true };
    bool m_isShadowRecive{ true };
	std::string m_modelName{};

	FoliageType() = default;
	~FoliageType() = default;

    FoliageType(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material,
        bool castShadow = true, const std::string& modelName = "")
        : m_mesh(std::move(mesh)), m_material(std::move(material)),
          m_castShadow(castShadow), m_modelName(modelName) {}
    bool operator==(const FoliageType& other) const
    {
        return m_mesh == other.m_mesh && m_material == other.m_material && m_castShadow == other.m_castShadow;
	}

};
