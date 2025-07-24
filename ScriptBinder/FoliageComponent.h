#pragma once
#include "FoliageBaseType.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "GameObject.h"
#include "Terrain.h"
#include "FoliageComponent.generated.h"

class FoliageComponent : public Component, public RegistableEvent<FoliageComponent>
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
    [[Property]]
    FileGuid m_foliageAssetGuid{};
    std::vector<FoliageType> m_foliageTypes{};
    std::vector<FoliageInstance> m_foliageInstances{};
};
