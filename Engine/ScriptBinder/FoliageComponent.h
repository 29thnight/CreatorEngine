#pragma once
#include "Core.Minimal.h"
#include "FoliageType.h"
#include "FoliageInstance.h"
#include "TerrainBuffers.h"
#include "Component.h"

class TerrainComponent;
class FoliageComponent : public meta::identity<FoliageComponent, Component>
{
    public:
    static consteval auto reflect()
    {
        return meta::schema<Self>(
            meta::field<&Self::m_foliageAssetGuid>);
    }
public:
    FoliageComponent() = default;

    void OnDeserialized(); // CT6-d: 폴리지 애셋·모델 로드(구 팩토리 분기)


    void OnInitialized() override;
    void OnUninitializing() override;

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 FoliageSystem(조밀 벡터, 전용
    // 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전, 근거는
    // AnimatorSystem.h 주석 참고). Awake/OnDestroy는 렌더 등록(scene->Collect
    // FoliageComponent, RegisterCommand)용으로 그대로 둔다(트랙 범위 밖).
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;

	// 저작 게시는 Editor Host가 소유한다. 여기서는 YAML payload만 만들고
	// 목적 경로 확정·원자적 쓰기·meta 생성은 AssetAuthoringPort 너머에서 끝난다.
	void SaveFoliageAsset(const file::path& directory, const std::wstring& name);
	void LoadFoliageAsset(FileGuid assetGuid);

    void AddFoliageType(const FoliageType& type);
    void RemoveFoliageType(uint32 typeID);

    void AddFoliageInstance(const FoliageInstance& instance);
    void RemoveFoliageInstance(size_t index);

    void AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance);
    void AddRandomInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush, uint32 typeID, int count);
    void RemoveInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush);

	void UpdateFoliageCullingData(const DirectX::BoundingFrustum& cameraFrustum);

    const std::vector<FoliageType>& GetFoliageTypes() const { return m_foliageTypes; }
    const std::vector<FoliageInstance>& GetFoliageInstances() const { return m_foliageInstances; }

    FileGuid m_foliageAssetGuid{};
private:
    std::vector<FoliageType> m_foliageTypes{};
    std::vector<FoliageInstance> m_foliageInstances{};
};
