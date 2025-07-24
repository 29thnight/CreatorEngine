#pragma once
#include <string>
#include "Model.h"
#include "FoliageType.generated.h"

struct FoliageType
{
    Mesh* m_mesh{ nullptr };
    Material* m_material{ nullptr };
    [[Property]]
    bool m_castShadow{ true };
    [[Property]]
	std::string m_modelName{};

   ReflectFoliageType
    [[Serializable]]
	FoliageType() = default;
	~FoliageType() = default;

    FoliageType(Mesh* mesh, Material* material, bool castShadow = true, const std::string& modelName = "")
        : m_mesh(mesh), m_material(material), m_castShadow(castShadow), m_modelName(modelName) {}
    FoliageType(Model* model, bool castShadow = true)
		: m_mesh(model->GetMesh(0)), m_material(model->GetMaterial(0)), m_castShadow(castShadow), m_modelName(model->name) {
	}
    bool operator==(const FoliageType& other) const
    {
        return m_mesh == other.m_mesh && m_material == other.m_material && m_castShadow == other.m_castShadow;
	}

};
