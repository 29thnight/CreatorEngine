#include "gcce_probe_cleanup.h"
// Source-only integration fixture, intentionally not executed in this change.
// Run in a dedicated initialized engine test host with a source-free CEMF3 set:
// one model, at least two distinct static mesh artifacts, and Lattice materials.
#include "DataSystem.h"
#include "FoliageComponent.h"
#include "PrimitiveRenderProxy.h"
#include "Material.h"
#include "MaterialGraphSceneInput.h"
#include "Assets/ModelGeometryPayload.h"
#include "Experiment/Cooked/ArtifactByteSource.h"

#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace RenderTest
{
    namespace
    {
        struct FoliageReadCounts final
        {
            std::mutex mutex{};
            std::map<std::string, unsigned> reads{};
            std::thread::id owner{ std::this_thread::get_id() };
            bool ownerThreadRead{};
        };

        class FoliageCountingSource final : public experiment::cooked::ArtifactByteSource
        {
        public:
            FoliageCountingSource(own::shared_owner<const ArtifactByteSource> source,
                own::shared_owner<FoliageReadCounts> counts)
                : source_(std::move(source)), counts_(std::move(counts))
            {
            }
            bool CaptureArtifact(std::string_view path, own::shared_owner<const ArtifactByteSource>& narrowed,
                std::string& error) const override
            {
                auto exact = source_;
                if (!experiment::cooked::CaptureArtifactSource(exact, path, error))
                {
                    return false;
                }
                narrowed = own::make_shared<const FoliageCountingSource>(std::move(exact), counts_);
                return true;
            }
            bool Size(std::string_view path, std::uint64_t& size, std::string& error) const override
            {
                return source_->Size(path, size, error);
            }
            bool ReadAt(std::string_view path, std::uint64_t offset, std::span<std::byte> out,
                std::string& error) const override
            {
                {
                    std::lock_guard lock(counts_->mutex);
                    ++counts_->reads[std::string(path)];
                    counts_->ownerThreadRead |= std::this_thread::get_id() == counts_->owner;
                }
                return source_->ReadAt(path, offset, out, error);
            }
        private:
            own::shared_owner<const ArtifactByteSource> source_{};
            own::shared_owner<FoliageReadCounts> counts_{};
        };

        void RequireFoliage(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }
    }

    // No shader compilation or renderer startup here. The caller supplies
    // already-cooked fixture bytes and initializes the ordinary engine services.
    bool RunFoliageAssetDepotProbe(std::span<const std::byte> manifest,
        own::shared_owner<const experiment::cooked::ArtifactByteSource> source,
        const experiment::cooked::AssetSetMountOptions& options, std::string& log)
    {
        std::vector<experiment::cooked::AssetManifestIssue> issues;
        AssetDepot::AssetMountId mount{};
        try
        {
            auto counts = own::make_shared<FoliageReadCounts>();
            source = own::make_shared<const FoliageCountingSource>(std::move(source), counts);
            mount = DataSystems->MountAssetSet(manifest, source, options, issues);
            RequireFoliage(mount.IsValid(), "Fixture AssetSet mount failed.");
            const auto roots = DataSystems->ListRootLinks<assets::ModelAnimationDescriptor>(mount);
            RequireFoliage(roots.size() == 1u, "Fixture must contain one model root.");
            {
                std::lock_guard lock(counts->mutex);
                RequireFoliage(counts->reads.empty(), "Mount/root enumeration opened payload bytes.");
            }
            DataSystems->SetModelAssetCacheBudgets(0u, 0u, 0u);
            DataSystems->SetModelGeometryCacheBudgets(0u, 0u);
            DataSystems->SetMaterialAssetCacheBudgets(0u, 0u);
            auto modelRequest = DataSystems->RequestAsync(roots.front());
            modelRequest.Completion().wait(); // Test harness only.
            auto model = modelRequest.Snapshot().asset;
            RequireFoliage(model && model->meshes.size() >= 2u, "Fixture needs two independent meshes.");
            {
                std::lock_guard lock(counts->mutex);
                RequireFoliage(counts->reads.size() == 1u, "Model descriptor acquisition loaded a child.");
            }
            const auto& chosen = model->summary.meshes[1u];
            RequireFoliage(!chosen.skinned && chosen.materialAssetId.IsValid(), "Fixture selection must be static/materialized.");
            RequireFoliage(model->meshes[0u].blob.artifactPath != model->meshes[1u].blob.artifactPath,
                "Fixture siblings must use distinguishable geometry artifacts.");
            FoliageType type;
            type.m_modelGuid = FileGuid(roots.front().identity.assetId.value);
            type.m_meshAssetId = FileGuid(chosen.meshAssetId.value);
            type.m_materialAssetId = FileGuid(chosen.materialAssetId.value);
            type.m_allowLegacySource = false;
            FoliageRenderProxy proxy;
            {
                gc::domain componentDomain;
                auto componentRoot = Component::CreateManaged<FoliageComponent>(componentDomain);
                FoliageComponent& component = *componentRoot;
                gcce_probe::cleanup componentCleanup(component);
                component.AddFoliageType(type);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (component.GetAssetBindingStatus(0u) == AssetDepot::AssetRequestStatus::Pending
                    && std::chrono::steady_clock::now() < deadline)
                {
                    component.PollAssetBindings();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Harness only.
                }
                RequireFoliage(component.GetAssetBindingStatus(0u) == AssetDepot::AssetRequestStatus::Ready,
                    "Selected foliage model/material did not become ready.");
                const auto& bound = component.GetFoliageTypes().front();
                RequireFoliage(bound.m_modelDescriptor && bound.m_meshDescriptor && !bound.m_modelGeneration
                    && bound.m_meshDescriptor->meshId == chosen.meshAssetId.value
                    && bound.m_material && bound.m_material->HasMaterialGraph(),
                    "Foliage retained a whole legacy model or chose the wrong mesh/material.");
                FoliageInstance instance;
                component.AddFoliageInstance(instance);
                proxy.m_foliageTypes = component.GetFoliageTypes();
                proxy.m_foliageInstances = component.GetFoliageInstances();
                proxy.RebuildInstanceMap();
            }
            RequireFoliage(DataSystems->UnmountAssetSet(mount, issues), "Fixture unmount failed.");
            mount = {};
            auto& selected = proxy.m_foliageTypes.front();
            auto geometryRequest = DataSystems->RequestAsync(selected.m_meshDescriptor);
            geometryRequest.Completion().wait(); // Exact old-owner rehydration after unmount.
            auto geometry = geometryRequest.Snapshot().asset;
            RequireFoliage(geometry && geometry->Matches(*selected.m_meshDescriptor),
                "Old selected geometry backing was lost after unmount/component destruction.");
            RequireFoliage(proxy.CaptureDrawSources().size() == 1u && selected.m_graphMaterialSource,
                "Proxy lost selected material/geometry owners.");
            RequireFoliage(!DataSystems->TryAcquire(roots.front()), "Unmount allowed a current root lookup.");
            auto unresolvedMaterial = DataSystems->RequestAsync(model, type.MaterialLink());
            RequireFoliage(unresolvedMaterial.Snapshot().error == AssetDepot::AssetRequestError::NotMounted,
                "An unresolved material silently followed a different resolver after unmount.");
            {
                std::lock_guard lock(counts->mutex);
                RequireFoliage(!counts->ownerThreadRead, "Foliage decoded/read payload on the owner thread.");
                RequireFoliage(!counts->reads.contains(model->meshes[0u].blob.artifactPath),
                    "Selected foliage loaded an unrelated mesh sibling.");
            }
            gc::domain absentDomain;
            auto absentRoot = Component::CreateManaged<FoliageComponent>(absentDomain);
            FoliageComponent& absent = *absentRoot;
            gcce_probe::cleanup absentCleanup(absent);
            absent.AddFoliageType(type);
            RequireFoliage(absent.GetAssetBindingStatus(0u) == AssetDepot::AssetRequestStatus::Failed,
                "Unmounted current binding did not terminate.");
            absent.PollAssetBindings();
            RequireFoliage(absent.GetAssetBindingStatus(0u) == AssetDepot::AssetRequestStatus::Failed,
                "Failed foliage binding retried automatically.");
            log = "Foliage source-free selected mesh/material ownership probe passed.";
            return true;
        }
        catch (const std::exception& error)
        {
            if (mount.IsValid())
            {
                (void)DataSystems->UnmountAssetSet(mount, issues);
            }
            log = error.what();
            return false;
        }
    }
}
