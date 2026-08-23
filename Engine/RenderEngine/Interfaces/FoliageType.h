#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include <string>

// 데이터 경계 헤더는 렌더 본체를 include하지 않는다 — Mesh·Material은
// 포인터로만 든다. (Model에서 만드는 편의 생성자는 유일 호출자였던 에디터
// 드래그드롭 쪽으로 전개해 걷어냈다.)
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
    Mesh* m_mesh{ nullptr };
    Material* m_material{ nullptr };
    bool m_castShadow{ true };
    bool m_isShadowRecive{ true };
	std::string m_modelName{};

	FoliageType() = default;
	~FoliageType() = default;

    FoliageType(Mesh* mesh, Material* material, bool castShadow = true, const std::string& modelName = "")
        : m_mesh(mesh), m_material(material), m_castShadow(castShadow), m_modelName(modelName) {}
    bool operator==(const FoliageType& other) const
    {
        return m_mesh == other.m_mesh && m_material == other.m_material && m_castShadow == other.m_castShadow;
	}

};
