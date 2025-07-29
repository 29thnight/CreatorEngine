#include "FoliageComponent.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Terrain.h"
#include "Scene.h"
#include "Camera.h"
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
            UpdateFoliageCullingData(camera);
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
    m_foliageInstances.push_back(instance);
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
    m_foliageInstances.push_back(inst);
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

void FoliageComponent::UpdateFoliageCullingData(Camera* camera)
{
    if (!camera) return;

    for (auto& foliage : m_foliageInstances)
    {
        Mathf::Vector3 position = foliage.m_position;
        Mathf::Vector3 rotation = foliage.m_rotation;
        Mathf::Vector3 scale = foliage.m_scale;

        foliage.m_worldMatrix = Mathf::Matrix::CreateScale(scale) *
            Mathf::Matrix::CreateRotationX(Mathf::ToRadians(rotation.x)) *
            Mathf::Matrix::CreateRotationY(Mathf::ToRadians(rotation.y)) *
            Mathf::Matrix::CreateRotationZ(Mathf::ToRadians(rotation.z)) *
            Mathf::Matrix::CreateTranslation(position);

        const FoliageType& foliageType = m_foliageTypes[foliage.m_foliageTypeID];
        Mesh* mesh = foliageType.m_mesh;
        if (mesh == nullptr) continue;

        DirectX::BoundingBox boundingBox = mesh->GetBoundingBox();
        DirectX::BoundingBox transformedBox;
        boundingBox.Transform(transformedBox, foliage.m_worldMatrix);

        if (camera->GetFrustum().Intersects(transformedBox))
        {
            foliage.m_isCulled = false;
        }
        else
        {
			foliage.m_isCulled = true;
        }
    }
}
