#pragma once
#include "Core.Minimal.h"
#include "FoliageType.h"
#include "FoliageInstance.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "FoliageComponent.generated.h"

class Camera;
class TerrainComponent;
class FoliageComponent : public Component, public RegistableEvent<FoliageComponent>
{
public:
    ReflectFoliageComponent
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(FoliageComponent)

    void Awake() override;
    void Update(float deltaTime) override;
    void OnDestroy() override;

	void SaveFoliageAsset(const file::path& savePath);
	void LoadFoliageAsset(FileGuid assetGuid);

    void AddFoliageType(const FoliageType& type);
    void RemoveFoliageType(uint32 typeID);

    void AddFoliageInstance(const FoliageInstance& instance);
    void RemoveFoliageInstance(size_t index);

    void AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance);
    void AddRandomInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush, uint32 typeID, int count);
    void RemoveInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush);

	void UpdateFoliageCullingData(Camera* camera);

    const std::vector<FoliageType>& GetFoliageTypes() const { return m_foliageTypes; }
    const std::vector<FoliageInstance>& GetFoliageInstances() const { return m_foliageInstances; }

    [[Property]]
    FileGuid m_foliageAssetGuid{};
private:
    std::vector<FoliageType> m_foliageTypes{};
    std::vector<FoliageInstance> m_foliageInstances{};
};
