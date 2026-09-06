#include "FoliageComponent.h"
#include "FoliageSystem.h"
#include "ExperimentMaterialMigration.h" // MBC9: generation 재질 시공
#include "Assets/ModelAssetGeneration.h"
#include "Material.h"
#include "DataSystem.h"
#include "Experiment/Model.h" // I5-D5c4: 재질 저작 정본 해석
#include "Interfaces/AssetAuthoringPort.h"
#include "AuthoringParsedDocument.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Terrain.h"
#include "Scene.h"
#include "Camera.h"
#include "SceneManager.h"
#include "Mathematics.Intersect.h"
#include <mathematics/transform.hpp>
#include <random>
#include <sstream>

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

// 트랙 C3 — FoliageSystem 등록/해지. Awake/OnDestroy(컴포넌트당 1회 게이트)가
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 AnimatorSystem.h 상단 주석 참고 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (Scene::FlushPendingDestroy·PrefabUtility::ApplyComponentDiff)도
// OnUninitializing(위 OnDestroy 브리지) 직전에 OnRemovingFromScene을 먼저
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
    auto scene = GetOwner()->m_ownerScene;
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
		Debug->LogError(
			"Foliage save requires a complete Editor authoring transaction");
		return;
	}

	m_foliageAssetGuid = result.guid;
	Debug->LogDebug("Foliage asset saved to: " + result.assetPath.string());
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
	std::cout << "Foliage asset loaded successfully: " << assetPath << std::endl;
}

void FoliageComponent::BindModelGeneration(FoliageType& type)
{
    // PHASE 3.75 MBC9 — typed 정본만 있다. Foliage 자산은 모델을 **이름**으로만
    // 적으므로(m_modelName) 이름 → ModelId → 현재 generation으로 잇는다. 메시는
    // legacy 규약과 같은 0번(FoliageType이 메시를 고르지 않는다). 재질은 그 메시가
    // 가리키는 generation 재질에서 시공하고 embedded texture는 같은 closure에서 푼다.
    type.m_modelGeneration.reset();
    type.m_modelMeshIndex = 0;
    type.m_material.reset();
    type.m_authoredMaterial.reset();
    if (type.m_modelName.empty()) return;

    const FileGuid modelGuid = DataSystems->GetStemToGuid(type.m_modelName);
    std::shared_ptr<const assets::ModelAssetGeneration> generation =
        DataSystems->LoadModelAssetGeneration(modelGuid);
    if (!generation || generation->Meshes().empty())
    {
        Debug->LogError("FoliageType 모델 generation 해석 실패: " + type.m_modelName);
        return;
    }
    const assets::ModelMeshAsset& mesh = generation->Meshes().front();
    const auto materials = generation->Materials();
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        if (materials[index].materialId != mesh.materialId) continue;
        experiment::Material converted;
        ExperimentMaterialMigration::ConvertModelMaterialAsset(
            materials[index], *generation, converted);
        auto material = std::make_shared<Material>();
        std::string error;
        if (ExperimentMaterialMigration::ConvertToLegacyMaterial(
            converted, nullptr, *material, error))
        {
            DataSystems->FinalizeMaterialRuntime(*material);
            DataSystems->BindModelGenerationTextures(*material, *generation);
            type.m_material = std::move(material);
            if (!materials[index].shaderAssetId.IsNil())
            {
                auto authored = std::make_shared<experiment::Material>(std::move(converted));
                type.m_authoredMaterial = std::move(authored);
            }
        }
        else
        {
            Debug->LogWarning("FoliageType 재질 변환 실패 — 빈 재질: " + error);
        }
        break;
    }
    type.m_modelGeneration = std::move(generation);
}

void FoliageComponent::AddFoliageType(const FoliageType& type)
{
    m_foliageTypes.push_back(type);
    // 저작 경로(에디터 드롭·CLI)든 자산 로드 경로(LoadFoliageAsset)든 이름으로
    // 같은 typed 바인딩을 한다.
    BindModelGeneration(m_foliageTypes.back());
	PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
}

void FoliageComponent::RemoveFoliageType(uint32 typeID)
{
    if (typeID < m_foliageTypes.size())
	{
        m_foliageTypes.erase(m_foliageTypes.begin() + typeID);
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
        sealed.RebuildWorldMatrix();
        m_foliageInstances.push_back(std::move(sealed));
		PublishRenderProxyDirty(ProxyDirty::Payload);
    }
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
//helper
std::vector<std::pair<size_t, size_t>> DivideRangeAuto(size_t count)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    if (count == 0)
        return ranges;

    unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 4; // 안전 기본값 (미검출 시)

    const size_t numSplits = hwThreads * 2 + 1;
    ranges.reserve(numSplits);

    const size_t chunk = (count + numSplits - 1) / numSplits; // ceil(count / numSplits)
    size_t begin = 0;

    for (size_t i = 0; i < numSplits; ++i)
    {
        size_t end = std::min(begin + chunk, count);
        if (begin >= end)
            break;
        ranges.emplace_back(begin, end);
        begin = end;
    }

    return ranges;
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
            // MBC9 — 바운드는 typed generation 메시가 소유한다.
            if (!foliageType.m_modelGeneration
                || foliageType.m_modelMeshIndex >= foliageType.m_modelGeneration->Meshes().size())
            {
                foliage.m_isCulled = true; // 안전 기본값
                continue;
            }

            if(SceneManagers->IsGameStart())
            {
				const math::aabb worldBounds = math::transform(
					foliageType.m_modelGeneration->Meshes()[foliageType.m_modelMeshIndex].bounds,
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

    auto ranges = DivideRangeAuto(m_foliageInstances.size());

    std::vector<std::future<void>> tasks;
    tasks.reserve(ranges.size());

    for (auto& [begin, end] : ranges)
    {
        tasks.emplace_back(std::async(std::launch::async, process_range, begin, end));
    }

    // 완료 대기
    for (auto& f : tasks)
    {
        if (f.valid()) f.get();
    }
}


void FoliageComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동 — m_foliageAssetGuid는 반영 멤버.
	if (m_foliageAssetGuid == nullFileGuid)
	{
		Debug->LogError("FoliageComponent is missing m_foliageAssetGuid");
		return;
	}

	LoadFoliageAsset(m_foliageAssetGuid);

	// MBC9 — 타입은 LoadFoliageAsset → AddFoliageType이 이미 typed 바인딩했다.
	// 여기서는 바인딩 실패(모델 부재)를 한 번 더 관측한다.
	for (const FoliageType& type : GetFoliageTypes())
	{
		if (!type.m_modelName.empty() && !type.m_modelGeneration)
		{
			Debug->LogError("Failed to load model for FoliageType: " + type.m_modelName);
		}
	}

	SetEnabled(true); // 구 분기 말미의 강제 활성 보존
}

