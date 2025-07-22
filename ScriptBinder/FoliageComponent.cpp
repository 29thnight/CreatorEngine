#include "FoliageComponent.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"

void FoliageComponent::Awake()
{
    auto scene = SceneManagers->GetActiveScene();
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

void FoliageComponent::OnDestroy()
{
    auto scene = SceneManagers->GetActiveScene();
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
