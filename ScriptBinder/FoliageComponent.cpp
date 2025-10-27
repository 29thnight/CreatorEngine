#include "FoliageComponent.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Terrain.h"
#include "Scene.h"
#include "Camera.h"
#include "SceneManager.h"
#include <random>

void FoliageComponent::Awake()
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

void FoliageComponent::Update(float deltaTime)
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene && renderScene)
    {
        auto camera = CameraManagement->GetLastCamera();
        if (camera)
        {
            UpdateFoliageCullingData(camera.get());
		}
	}
}

void FoliageComponent::OnDestroy()
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

void FoliageComponent::SaveFoliageAsset(const file::path& savePath)
{
	file::path path = savePath.string() + ".foliage";
	std::ofstream outFile(path);
    if (!outFile.is_open())
    {
        std::cerr << "Failed to open file for saving foliage asset: " << path << std::endl;
        return;
	}

	MetaYml::Node assetNode;
    for (auto& type : m_foliageTypes)
    {
        MetaYml::Node typeNode = Meta::Serialize(&type);
        assetNode["FoliageAsset"]["Types"].push_back(typeNode);
	}

    for (auto& instance : m_foliageInstances)
    {
        MetaYml::Node instanceNode = Meta::Serialize(&instance);
        assetNode["FoliageAsset"]["Instances"].push_back(instanceNode);
	}

    outFile << assetNode;
    outFile.close();
    outFile.flush();
	DataSystems->ForceCreateYamlMetaFile(path);

	std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Ensure file is written before next operation
	FileGuid metaGuid = DataSystems->GetFileGuid(path);
    if (metaGuid != nullFileGuid)
    {
        m_foliageAssetGuid = metaGuid;
        assetNode["FoliageAsset"]["Guid"] = m_foliageAssetGuid.ToString();
    }
    else
    {
        std::cerr << "Failed to generate GUID for foliage asset: " << path << std::endl;
        return;
	}

	std::cout << "Foliage asset saved successfully: " << path << std::endl;
}

void FoliageComponent::LoadFoliageAsset(FileGuid assetGuid)
{
    auto assetPath = DataSystems->GetFilePath(assetGuid);
    if (assetPath.empty())
    {
        std::cerr << "Asset GUID not found: " << assetGuid.ToString() << std::endl;
        return;
    }

    MetaYml::Node assetNode = MetaYml::LoadFile(assetPath.string());
    if (assetNode.IsNull() || !assetNode["FoliageAsset"])
    {
        std::cerr << "Invalid foliage asset file: " << assetPath << std::endl;
        return;
    }

    m_foliageTypes.clear();
    m_foliageInstances.clear();
    for (const auto& typeNode : assetNode["FoliageAsset"]["Types"])
    {
        FoliageType type;
        Meta::Deserialize(&type, typeNode);
        AddFoliageType(type);
    }

    for (const auto& instanceNode : assetNode["FoliageAsset"]["Instances"])
    {
        FoliageInstance instance;
        Meta::Deserialize(&instance, instanceNode);
        AddFoliageInstance(instance);
    }
	std::cout << "Foliage asset loaded successfully: " << assetPath << std::endl;
}

void FoliageComponent::AddFoliageType(const FoliageType& type)
{
    m_foliageTypes.push_back(type);
}

void FoliageComponent::RemoveFoliageType(uint32 typeID)
{
    if (typeID < m_foliageTypes.size())
        m_foliageTypes.erase(m_foliageTypes.begin() + typeID);
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
        m_foliageInstances.push_back(instance);
    }
}

void FoliageComponent::RemoveFoliageInstance(size_t index)
{
    if(index < m_foliageInstances.size())
        m_foliageInstances.erase(m_foliageInstances.begin()+index);
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
    m_foliageInstances.erase(std::remove_if(m_foliageInstances.begin(), m_foliageInstances.end(),
        [&](const FoliageInstance& inst)
        {
            float dx = inst.m_position.x - brush.m_center.x;
            float dz = inst.m_position.z - brush.m_center.y;
            return dx * dx + dz * dz <= brush.m_radius * brush.m_radius;
        }), m_foliageInstances.end());
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

void FoliageComponent::UpdateFoliageCullingData(Camera* camera)
{
    if (!camera) return;
    if (m_foliageTypes.empty()) return;

    const size_t count = m_foliageInstances.size();
    if (count == 0) return;

    // 프러스텀은 값 캡처(스레드 안전)
    const auto frustum = camera->GetFrustum();

    auto process_range = [&](size_t begin, size_t end)
    {
        for (size_t i = begin; i < end; ++i)
        {
            if (i >= m_foliageInstances.size()) return;

            auto& foliage = m_foliageInstances[i];

            // 경계 체크 보정: >=
            if (static_cast<size_t>(foliage.m_foliageTypeID) >= m_foliageTypes.size())
                continue;

            const Mathf::Vector3 position = foliage.m_position;
            const Mathf::Vector3 rotation = foliage.m_rotation;
            const Mathf::Vector3 scale = foliage.m_scale;

            foliage.m_worldMatrix =
                Mathf::Matrix::CreateScale(scale) *
                Mathf::Matrix::CreateRotationX(Mathf::ToRadians(rotation.x)) *
                Mathf::Matrix::CreateRotationY(Mathf::ToRadians(rotation.y)) *
                Mathf::Matrix::CreateRotationZ(Mathf::ToRadians(rotation.z)) *
                Mathf::Matrix::CreateTranslation(position);

            const FoliageType& foliageType = m_foliageTypes[foliage.m_foliageTypeID];
            Mesh* mesh = foliageType.m_mesh;
            if (!mesh)
            {
                foliage.m_isCulled = true; // 안전 기본값
                continue;
            }

            if(SceneManagers->IsGameStart())
            {
                DirectX::BoundingBox box = mesh->GetBoundingBox();
                DirectX::BoundingBox tbox;
                box.Transform(tbox, foliage.m_worldMatrix);

                foliage.m_isCulled = !frustum.Intersects(tbox);
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
