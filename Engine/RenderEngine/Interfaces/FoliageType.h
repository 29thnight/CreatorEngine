#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include <memory>
#include <string>

// 데이터 경계 헤더는 렌더 본체를 include하지 않는다. shared_ptr는 불완전 타입을
// 보관할 수 있으므로 Mesh·Material 정의 없이도 소유권 계약을 표현할 수 있다.
class Mesh;
class Material;

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
