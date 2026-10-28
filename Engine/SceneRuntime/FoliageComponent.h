#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "FoliageType.h"
#include "FoliageInstance.h"
#include "FoliageCulling.h"
#include "TerrainBuffers.h"
#include "Component.h"
#include "AssetDepot/AssetRequest.h"
#include <mathematics/frustum.hpp>
#include "../Utility_Framework/Foundation/StandardLibrary.h"

class TerrainComponent;
class [[reflgen::reflect, reflgen::public_methods]] FoliageComponent : public meta::identity<FoliageComponent, Component>
{
    public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
    FoliageComponent();
    ~FoliageComponent() override;

    [[reflgen::script_internal]]
    void OnDeserialized(); // CT6-d: 폴리지 애셋·모델 로드(구 팩토리 분기)


    [[reflgen::script_internal]]
    void OnInitialized() override;
    [[reflgen::script_internal]]
    void OnUninitializing() override;

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 FoliageSystem(조밀 벡터, 전용
    // 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전, 근거는
    // AnimatorSystem.h 주석 참고). OnInitialized/OnUninitializing은 렌더 등록(scene->Collect
    // FoliageComponent, RegisterCommand)용으로 그대로 둔다(트랙 범위 밖).
    [[reflgen::script_internal]]
    void OnAddedToScene() override;
    [[reflgen::script_internal]]
    void OnRemovingFromScene() override;

	// 저작 게시는 Editor Host가 소유한다. 여기서는 YAML payload만 만들고
	// 목적 경로 확정·원자적 쓰기·meta 생성은 AssetAuthoringPort 너머에서 끝난다.
	void SaveFoliageAsset(const std::filesystem::path& directory, const std::wstring& name);
	void LoadFoliageAsset(FileGuid assetGuid);

    void AddFoliageType(const FoliageType& type);
    void RemoveFoliageType(uint32 typeID);

    // Explicit rebind is the only retry after Failed, Stale or Cancelled.
    void RebindFoliageType(uint32 typeID);
    void PollAssetBindings();
    [[nodiscard]] AssetDepot::AssetRequestStatus GetAssetBindingStatus(uint32 typeID) const;

    void AddFoliageInstance(const FoliageInstance& instance);
    // Owner-thread transform edit. Identity is retained across submitted poses.
    bool UpdateFoliageInstance(std::uint64_t identity, const math::vector3& position,
        const math::vector3& rotation, const math::vector3& scale);
    void RemoveFoliageInstance(size_t index);

    void AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance);
    void AddRandomInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush, uint32 typeID, int count);
    void RemoveInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush);

	void UpdateFoliageCullingData(
		const std::optional<math::bounding_frustum>& cameraFrustum);

    const ce::dynamic_array<FoliageType>& GetFoliageTypes() const { return m_foliageTypes; }
    const ce::dynamic_array<FoliageInstance>& GetFoliageInstances() const { return m_foliageInstances; }

    // Owner-thread development measurement. Disabled by default; samples use
    // preallocated storage and never allocate in the measured frame loop.
    void ConfigureCullingProbe(bool cached);
    std::span<const double> CullingProbeSamples() const { return m_cullingProbeSamples; }
    std::span<const double> CullingProbeFrameSamples() const { return m_cullingProbeFrames; }
    size_t CullingCacheBytes() const { return m_worldBoundsCache.AllocatedBytes(); }
    uint64_t CullingProbeTransforms() const { return m_cullingProbeTransforms; }

    FileGuid m_foliageAssetGuid{};
private:
    struct AssetBinding;
    void CancelAssetBindings();
    [[reflgen::ignore]]
    ce::dynamic_array<own::unique_owner<AssetBinding>> m_assetBindings{};

    [[reflgen::ignore]]
    ce::dynamic_array<FoliageType> m_foliageTypes{};

    [[reflgen::ignore]]
    ce::dynamic_array<FoliageInstance> m_foliageInstances{};
    [[reflgen::ignore]]
    foliage::WorldBoundsCache m_worldBoundsCache{};
    [[reflgen::ignore]]
    bool m_cullingProbeEnabled{false};
    [[reflgen::ignore]]
    bool m_cullingProbeCached{true};
    [[reflgen::ignore]]
    ce::dynamic_array<double> m_cullingProbeSamples{};
    [[reflgen::ignore]]
    uint64_t m_cullingProbeTransforms{};
    [[reflgen::ignore]]
    ce::dynamic_array<double> m_cullingProbeFrames{};
    [[reflgen::ignore]]
    std::chrono::steady_clock::time_point m_cullingProbeLastFrame{};
private:
    [[reflgen::ignore]]
    void ReleaseManagedResources() override;

};
