#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IAwakable.h"
#include "IOnDestroy.h"
#include "Mesh.h"
#include "GameObject.h"
#include "Terrain.h"
#include "FoliageComponent.generated.h"

struct FoliageType
{
    Mesh* m_mesh{ nullptr };
    bool m_castShadow{ true };
};

struct FoliageInstance
{
    Mathf::Vector3 m_position{};
    Mathf::Vector3 m_rotation{}; // Euler angles
    Mathf::Vector3 m_scale{ 1.f,1.f,1.f };
    uint32 m_foliageTypeID{ 0 }; // index of FoliageType
};

class FoliageComponent : public Component, public IAwakable, public IOnDestroy
{
public:
    ReflectFoliageComponent
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(FoliageComponent)

    void Awake() override;
    void OnDestroy() override;

    void AddFoliageType(const FoliageType& type);
    void RemoveFoliageType(uint32 typeID);

    void AddFoliageInstance(const FoliageInstance& instance);
    void RemoveFoliageInstance(size_t index);

    void AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance);

    const std::vector<FoliageType>& GetFoliageTypes() const { return m_foliageTypes; }
    const std::vector<FoliageInstance>& GetFoliageInstances() const { return m_foliageInstances; }

private:
    FileGuid m_foliageAssetGuid{};
    std::vector<FoliageType> m_foliageTypes{};
    std::vector<FoliageInstance> m_foliageInstances{};
};
