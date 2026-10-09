#include "FoliageComponent.h"
#include "FoliageSystem.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelAnimationDescriptor.h"
#include "Assets/ModelMeshDescriptor.h"
#include "MaterialGraphSceneInput.h"
#include "Material.h"
#include "DataSystem.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "AuthoringParsedDocument.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Terrain.h"
#include "Scene.h"
#include "Mathematics.Intersect.h"
#include <mathematics/transform.hpp>
#include <random>
#include <algorithm>

struct FoliageComponent::AssetBinding
{
    AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> model{};
    AssetDepot::AssetRequest<assets::ModelMeshDescriptor> mesh{};
    AssetDepot::AssetRequest<Material> material{};
    DataSystem::ModelPreparation legacyPreparation{};
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor{};
    own::shared_owner<const assets::ModelMeshDescriptor> selectedMesh{};
    own::shared_owner<const assets::ModelAssetGeneration> legacyGeneration{};
    AssetDepot::AssetRequestStatus status{ AssetDepot::AssetRequestStatus::Pending };
    AssetDepot::AssetRequestError error{ AssetDepot::AssetRequestError::None };
    std::string message{};
    std::size_t meshIndex{};
    bool childrenRequested{};

    void Cancel()
    {
        model.Cancel();
        mesh.Cancel();
        material.Cancel();
        if (status == AssetDepot::AssetRequestStatus::Pending)
        {
            status = AssetDepot::AssetRequestStatus::Cancelled;
        }
        // Accepted jobs retain their own captures in DataSystem's shutdown
        // barrier. Dropping this subscriber is not proof the work has drained.
        model = {};
        mesh = {};
        material = {};
        descriptor.reset();
        selectedMesh.reset();
        legacyGeneration.reset();
        legacyPreparation.reset();
    }
};

FoliageComponent::FoliageComponent() = default;
FoliageComponent::~FoliageComponent() = default;

void FoliageComponent::ReleaseManagedResources()
{
    CancelAssetBindings();
    m_assetBindings.clear();
}

void FoliageComponent::CancelAssetBindings()
{
    for (auto& binding : m_assetBindings)
    {
        if (binding)
        {
            binding->Cancel();
        }
    }
}

AssetDepot::AssetRequestStatus FoliageComponent::GetAssetBindingStatus(uint32 typeID) const
{
    return typeID < m_assetBindings.size() && m_assetBindings[typeID]
        ? m_assetBindings[typeID]->status : AssetDepot::AssetRequestStatus::Failed;
}

void FoliageComponent::OnInitialized()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if(scene)
    {
        scene->CollectFoliageComponent(this);
        if(renderScene)
        {
            renderScene->RegisterCommand(this);
        }
    }
}

// 트랙 C3 — FoliageSystem 등록/해지. OnInitialized/OnUninitializing(컴포넌트당 1회 게이트)이
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 AnimatorSystem.h 상단 주석 참고 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (Scene::FlushPendingDestroy·PrefabUtility::ApplyComponentDiff)도
// OnUninitializing 직전에 OnRemovingFromScene을 먼저
// 부르므로, 이 시스템에서 빠지는 시점이 항상 실 파괴보다 먼저다.
void FoliageComponent::OnAddedToScene()
{
    FoliageSystems->Register(this);
	if (HasLifecycleState(State_Initialized) && GetOwner())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->CollectFoliageComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->RegisterCommand(this);
		}
	}
}

void FoliageComponent::OnRemovingFromScene()
{
    FoliageSystems->Unregister(this);
	if (GetOwner() && !GetOwner()->IsDestroyMark())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->UnCollectFoliageComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->UnregisterCommand(this);
		}
	}
}

void FoliageComponent::OnUninitializing()
{
    ReleaseManagedResources();
    auto scene = GetOwner() ? GetOwner()->m_ownerScene : nullptr;
    auto renderScene = SceneManagers->GetRenderScene();
    if(scene)
    {
        scene->UnCollectFoliageComponent(this);
        if(renderScene)
        {
            renderScene->UnregisterCommand(this);
        }
    }
}

void FoliageComponent::SaveFoliageAsset(const file::path& directory,
	const std::wstring& name)
{
	// 빈 시퀀스를 명시적으로 만든다. 손대지 않은 노드를 그대로 흘리면
	// 0바이트를 내보내는데, 그렇게 저장된 자산은 LoadFoliageAsset의
	// assetNode["FoliageAsset"] 검사에서 다시 열리지 않는다.
	Authoring::WriteDocument document;
	const Authoring::WriteNode foliageNode = document.Root().Child("FoliageAsset");
	const Authoring::WriteNode typesNode = foliageNode.Child("Types");
	typesNode.SetSequence();
	for (auto& type : m_foliageTypes)
	{
		Meta::SerializeInto(&type, typesNode.Append());
	}

	const Authoring::WriteNode instancesNode = foliageNode.Child("Instances");
	instancesNode.SetSequence();
	for (auto& instance : m_foliageInstances)
	{
		Meta::SerializeInto(&instance, instancesNode.Append());
	}

	TextAssetAuthoringRequest request{};
	request.destinationDirectory = directory;
	request.name = name;
	request.payload = document.Dump();

	TextAssetAuthoringResult result{};
	if (!AssetAuthoringPort::WriteFoliage(request, result))
	{
		Debug::PrintLog(spdlog::level::err,
			"Foliage save requires a complete Editor authoring transaction");
		return;
	}

	m_foliageAssetGuid = result.guid;
	Debug::PrintLog(spdlog::level::debug, "Foliage asset saved to: " + result.assetPath.string());
}

void FoliageComponent::LoadFoliageAsset(FileGuid assetGuid)
{
    auto assetPath = DataSystems->GetFilePath(assetGuid);
    if (assetPath.empty())
    {
        std::cerr << "Asset GUID not found: " << assetGuid.ToString() << std::endl;
        return;
    }

    std::string parseError;
    const Authoring::ParsedDocument document =
        Authoring::ParsedDocument::ParseFile(assetPath.string(), parseError);
    const Authoring::ReadNode assetNode = document.Root();
    if (!document || assetNode.IsNull() || !assetNode["FoliageAsset"])
    {
        std::cerr << "Invalid foliage asset file: " << assetPath
            << " (" << parseError << ")" << std::endl;
        return;
    }

    CancelAssetBindings();
    m_assetBindings.clear();
    m_foliageTypes.clear();
    m_foliageInstances.clear();
    for (const Authoring::ReadNode typeNode :
        assetNode["FoliageAsset"]["Types"])
    {
        FoliageType type;
        Meta::Deserialize(&type, typeNode);
        AddFoliageType(type);
    }

    for (const Authoring::ReadNode instanceNode :
        assetNode["FoliageAsset"]["Instances"])
    {
        FoliageInstance instance;
        Meta::Deserialize(&instance, instanceNode);
        AddFoliageInstance(instance);
    }
    PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
	Debug::PrintLog(spdlog::level::info, "Foliage asset loaded successfully: {}", assetPath.string());
}

void FoliageComponent::RebindFoliageType(uint32 typeID)
{
    if (typeID >= m_foliageTypes.size())
    {
        return;
    }
    if (m_assetBindings[typeID])
    {
        m_assetBindings[typeID]->Cancel();
    }
    auto binding = own::make_unique<AssetBinding>();
    auto& type = m_foliageTypes[typeID];
    // One-time migration of name-only authoring data. A persisted typed link
    // never consults stem lookup, even if its current mount is absent.
    if (type.m_modelGuid == FileGuid{} && !type.m_modelName.empty())
    {
        type.m_modelGuid = DataSystems->GetStemToGuid(type.m_modelName);
        type.m_allowLegacySource = true;
    }
    const auto link = type.ModelLink();
    const auto catalog = DataSystems->GetCookedCatalog();
    experiment::cooked::ResolvedAssetEntry resolved;
    const bool mounted = catalog && catalog->Find(link.ToReference(), resolved)
        != experiment::cooked::AssetLookupStatus::NotMounted;
    if (mounted || !type.m_allowLegacySource || !PathFinder::IsAssetAuthoringEnabled())
    {
        // A wrong-kind/unsupported/missing record stays an explicit typed error.
        binding->model = DataSystems->RequestAsync(link);
    }
    else
    {
        const auto source = DataSystems->GetFilePath(type.m_modelGuid);
        if (link.IsValid() && !source.empty())
        {
            try
            {
                binding->legacyPreparation = DataSystems->PrepareModelAssetByPath(source.string(), false);
            }
            catch (const std::exception& error)
            {
                binding->message = error.what();
            }
        }
        if (!binding->legacyPreparation)
        {
            binding->status = AssetDepot::AssetRequestStatus::Failed;
            binding->error = AssetDepot::AssetRequestError::NotMounted;
            binding->message = "Foliage legacy source is unavailable; bind a mounted model link. " + binding->message;
            Debug::PrintLog(spdlog::level::err, binding->message);
        }
    }
    m_assetBindings[typeID] = std::move(binding);
    PollAssetBindings();
}

void FoliageComponent::PollAssetBindings()
{
    using Status = AssetDepot::AssetRequestStatus;
    using Error = AssetDepot::AssetRequestError;
    for (std::size_t index = 0u; index < m_assetBindings.size(); ++index)
    {
        auto& binding = *m_assetBindings[index];
        if (binding.status != Status::Pending)
        {
            continue;
        }
        auto& type = m_foliageTypes[index];
        const auto fail = [&](Status status, Error error, std::string message)
        {
            binding.Cancel();
            binding.status = status;
            binding.error = error;
            binding.message = std::move(message);
            Debug::PrintLog(spdlog::level::err, "Foliage binding failed: "
                + type.m_modelGuid.ToString() + " " + binding.message);
        };
        if (!binding.childrenRequested)
        {
            if (binding.legacyPreparation)
            {
                if (!DataSystems->ModelPreparationCompletion(binding.legacyPreparation).is_complete())
                {
                    continue;
                }
                std::string error;
                binding.legacyGeneration = DataSystems->ReadPreparedModel(binding.legacyPreparation, error);
                if (!binding.legacyGeneration || DataSystems->HasPreparedModelScene(binding.legacyPreparation))
                {
                    fail(Status::Stale, Error::RevisionChanged,
                        "Legacy foliage preparation changed or failed; rebind explicitly. " + error);
                    continue;
                }
                const auto meshes = binding.legacyGeneration->Meshes();
                const auto selected = type.m_meshAssetId == FileGuid{} ? meshes.begin()
                    : std::ranges::find(meshes, type.m_meshAssetId.m_guid, &assets::ModelMeshAsset::meshId);
                if (selected == meshes.end())
                {
                    fail(Status::Failed, Error::InvalidLink, "Selected legacy foliage mesh is absent.");
                    continue;
                }
                binding.meshIndex = static_cast<std::size_t>(selected - meshes.begin());
                type.m_meshAssetId = FileGuid(selected->meshId);
                if (type.m_materialAssetId == FileGuid{})
                {
                    type.m_materialAssetId = FileGuid(selected->materialId);
                }
                binding.material = DataSystems->RequestLegacyModelMaterialAsync(
                    binding.legacyGeneration, type.m_materialAssetId);
                binding.legacyPreparation.reset();
            }
            else
            {
                const auto result = binding.model.Snapshot();
                if (result.status == Status::Pending)
                {
                    continue;
                }
                if (result.status != Status::Ready || !result.asset)
                {
                    fail(result.status == Status::Ready ? Status::Failed : result.status,
                        result.status == Status::Ready ? Error::DecodeFailed : result.error, result.message);
                    continue;
                }
                binding.descriptor = result.asset;
                if (type.m_modelName.empty())
                {
                    type.m_modelName = binding.descriptor->summary.name;
                }
                const auto& meshes = binding.descriptor->summary.meshes;
                const auto selected = type.m_meshAssetId == FileGuid{} ? meshes.begin()
                    : std::ranges::find_if(meshes, [&](const auto& mesh)
                    {
                        return mesh.meshAssetId.value == type.m_meshAssetId.m_guid;
                    });
                if (selected == meshes.end() || selected->skinned)
                {
                    fail(Status::Failed, Error::UnsupportedRepresentation,
                        "Foliage needs a selected static mesh in its model descriptor.");
                    continue;
                }
                binding.meshIndex = static_cast<std::size_t>(selected - meshes.begin());
                type.m_meshAssetId = FileGuid(selected->meshAssetId.value);
                if (type.m_materialAssetId == FileGuid{})
                {
                    type.m_materialAssetId = FileGuid(selected->materialAssetId.value);
                }
                binding.selectedMesh = DataSystems->TryAcquire(binding.descriptor, binding.meshIndex);
                if (!binding.selectedMesh)
                {
                    binding.mesh = DataSystems->RequestAsync(binding.descriptor, binding.meshIndex);
                }
                binding.material = DataSystems->RequestAsync(binding.descriptor, type.MaterialLink());
                binding.model = {};
            }
            binding.childrenRequested = true;
        }
        if (binding.descriptor && !binding.selectedMesh)
        {
            const auto result = binding.mesh.Snapshot();
            if (result.status == Status::Pending)
            {
                continue;
            }
            if (result.status != Status::Ready || !result.asset)
            {
                fail(result.status == Status::Ready ? Status::Failed : result.status,
                    result.status == Status::Ready ? Error::DecodeFailed : result.error, result.message);
                continue;
            }
            binding.selectedMesh = result.asset;
            binding.mesh = {};
        }
        const auto material = binding.material.Snapshot();
        if (material.status == Status::Pending)
        {
            continue;
        }
        if (material.status != Status::Ready || !material.asset)
        {
            fail(material.status == Status::Ready ? Status::Failed : material.status,
                material.status == Status::Ready ? Error::DecodeFailed : material.error, material.message);
            continue;
        }
        if (!material.asset->HasMaterialGraph())
        {
            fail(Status::Failed, Error::UnsupportedRepresentation,
                "Live foliage requires a cooked Lattice graph material; Code-only materials are unsupported.");
            continue;
        }
        if (binding.descriptor && (!material.asset->GetAssetOrigin()
            || material.asset->GetAssetOrigin()->resolved.resolverRevision != binding.descriptor->origin.resolverRevision))
        {
            fail(Status::Stale, Error::RevisionChanged, "Foliage model/material resolver revisions differ.");
            continue;
        }
        // Allocate the source snapshot before moving any replacement owner;
        // a failed allocation must not leave a partially published type.
        auto graphSource = material_graph::SceneMaterialSource::Capture(*material.asset);
        // Publish one coherent selected replacement. Until here old proxy/scene
        // owners remain valid. No geometry/image payload is retained by the type.
        type.m_modelDescriptor = std::move(binding.descriptor);
        type.m_meshDescriptor = std::move(binding.selectedMesh);
        type.m_modelGeneration = std::move(binding.legacyGeneration);
        type.m_modelMeshIndex = static_cast<std::uint32_t>(binding.meshIndex);
        type.m_material = material.asset;
        type.m_graphMaterialSource = std::move(graphSource);
        type.m_authoredMaterial.reset();
        binding.material = {};
        binding.status = Status::Ready;
        PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
    }
}

void FoliageComponent::AddFoliageType(const FoliageType& type)
{
    FoliageType candidate = type;
    candidate.m_modelDescriptor.reset();
    candidate.m_meshDescriptor.reset();
    candidate.m_modelGeneration.reset();
    candidate.m_material.reset();
    candidate.m_graphMaterialSource.reset();
    candidate.m_authoredMaterial.reset();
    m_assetBindings.push_back(own::make_unique<AssetBinding>());
    try
    {
        m_foliageTypes.push_back(std::move(candidate));
    }
    catch (...)
    {
        m_assetBindings.pop_back();
        throw;
    }
    RebindFoliageType(static_cast<uint32>(m_foliageTypes.size() - 1u));
    PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
}

void FoliageComponent::RemoveFoliageType(uint32 typeID)
{
    if (typeID < m_foliageTypes.size())
    {
        m_assetBindings[typeID]->Cancel();
        m_assetBindings.erase(m_assetBindings.begin() + typeID);
        m_foliageTypes.erase(m_foliageTypes.begin() + typeID);
        std::erase_if(m_foliageInstances, [typeID](const auto& instance)
        {
            return instance.m_foliageTypeID == typeID;
        });
        for (auto& instance : m_foliageInstances)
        {
            if (instance.m_foliageTypeID > typeID)
            {
                --instance.m_foliageTypeID;
            }
        }
        PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
    }
}

void FoliageComponent::AddFoliageInstance(const FoliageInstance& instance)
{
    auto found = std::ranges::find_if(m_foliageInstances,
        [&](const FoliageInstance& existing)
        {
            return existing.m_position == instance.m_position;
        });

    if (found == m_foliageInstances.end())
    {
        FoliageInstance sealed = instance;
        // Insertion creates a new instance, even when a caller reuses a copied
        // template. Snapshot copies and later edits preserve this fresh identity.
        sealed.m_temporalIdentity = FoliageInstance::s_nextTemporalIdentity.fetch_add(
            1, std::memory_order_relaxed);
        sealed.RebuildWorldMatrix();
        m_foliageInstances.push_back(std::move(sealed));
		PublishRenderProxyDirty(ProxyDirty::Payload);
    }
}

bool FoliageComponent::UpdateFoliageInstance(std::uint64_t identity,
    const math::vector3& position, const math::vector3& rotation, const math::vector3& scale)
{
    const auto found = std::ranges::find_if(m_foliageInstances,
        [identity](const FoliageInstance& instance)
        {
            return identity != 0 && instance.m_temporalIdentity == identity;
        });
    if (found == m_foliageInstances.end())
    {
        return false;
    }
    found->m_position = position;
    found->m_rotation = rotation;
    found->m_scale = scale;
    found->RebuildWorldMatrix();
    PublishRenderProxyDirty(ProxyDirty::Payload);
    return true;
}

void FoliageComponent::RemoveFoliageInstance(size_t index)
{
    if(index < m_foliageInstances.size())
	{
        m_foliageInstances.erase(m_foliageInstances.begin()+index);
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}
}

void FoliageComponent::AddInstanceFromTerrain(TerrainComponent* terrain, const FoliageInstance& instance)
{
    if(!terrain) { return; }
    FoliageInstance inst = instance;
    float* heightMap = terrain->GetHeightMap();
    int width = terrain->GetWidth();
    int height = terrain->GetHeight();
    int x = static_cast<int>(std::clamp(instance.m_position.x, 0.f, static_cast<float>(width-1)));
    int y = static_cast<int>(std::clamp(instance.m_position.z, 0.f, static_cast<float>(height-1)));
    int idx = y * width + x;
    inst.m_position.y = heightMap[idx];
    AddFoliageInstance(inst);
}

void FoliageComponent::AddRandomInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush, uint32 typeID, int count)
{
    if (!terrain || count <= 0) return;

    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<float> offset(-brush.m_radius, brush.m_radius);
    std::uniform_real_distribution<float> rot(0.f, 360.f);
    std::uniform_real_distribution<float> scl(0.8f, 1.2f);

    for (int i = 0; i < count; ++i)
    {
        float dx = offset(gen);
        float dz = offset(gen);
        if (dx * dx + dz * dz > brush.m_radius * brush.m_radius)
        {
            --i;
            continue;
        }

        FoliageInstance inst;
        inst.m_position = { brush.m_center.x + dx, 0.f, brush.m_center.y + dz };
        inst.m_rotation = { 0.f, rot(gen), 0.f };
        float s = scl(gen);
        inst.m_scale = { s, s, s };
        inst.m_foliageTypeID = typeID;
        AddInstanceFromTerrain(terrain, inst);
    }
}

void FoliageComponent::RemoveInstancesInBrush(TerrainComponent* terrain, const TerrainBrush& brush)
{
	(void)terrain;
	const size_t previousSize = m_foliageInstances.size();
    m_foliageInstances.erase(std::remove_if(m_foliageInstances.begin(), m_foliageInstances.end(),
        [&](const FoliageInstance& inst)
        {
            float dx = inst.m_position.x - brush.m_center.x;
            float dz = inst.m_position.z - brush.m_center.y;
            return dx * dx + dz * dz <= brush.m_radius * brush.m_radius;
        }), m_foliageInstances.end());
	if (m_foliageInstances.size() != previousSize)
		PublishRenderProxyDirty(ProxyDirty::Payload);
}
void FoliageComponent::UpdateFoliageCullingData(
    const std::optional<math::bounding_frustum>& cameraFrustum)
{
    if (m_foliageTypes.empty()) return;

    const size_t count = m_foliageInstances.size();
    if (count == 0) return;

    auto process_range = [&](size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            if (i >= m_foliageInstances.size()) return;

            auto& foliage = m_foliageInstances[i];

 // 경계 체크 보정: >=
            if (static_cast<size_t>(foliage.m_foliageTypeID) >= m_foliageTypes.size())
                continue;

            foliage.RebuildWorldMatrix();

            const FoliageType& foliageType = m_foliageTypes[foliage.m_foliageTypeID];
            const auto* bounds = foliageType.m_meshDescriptor ? &foliageType.m_meshDescriptor->bounds
                : foliageType.m_modelGeneration && foliageType.m_modelMeshIndex < foliageType.m_modelGeneration->Meshes().size()
                    ? &foliageType.m_modelGeneration->Meshes()[foliageType.m_modelMeshIndex].bounds : nullptr;
            if (!bounds)
            {
                foliage.m_isCulled = true;
                continue;
            }

            if(SceneManagers->IsGameStart())
            {
				const math::aabb worldBounds = math::transform(
					*bounds,
					foliage.m_worldMatrix);
				foliage.m_isCulled = cameraFrustum.has_value() &&
					!worldBounds.is_empty() &&
					!math::intersects(*cameraFrustum, worldBounds);
            }
            else
            {
				foliage.m_isCulled = false;
            }
        }
    };

    // Bounds-only owner-thread work avoids queued raw component captures and
    // a blocking job barrier on the scene/UI thread.
    process_range(0u, count);
}


void FoliageComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동 — m_foliageAssetGuid는 반영 멤버.
	if (m_foliageAssetGuid == nullFileGuid)
	{
		Debug::PrintLog(spdlog::level::err, "FoliageComponent is missing m_foliageAssetGuid");
		return;
	}

	LoadFoliageAsset(m_foliageAssetGuid);

	SetEnabled(true); // 구 분기 말미의 강제 활성 보존
}

