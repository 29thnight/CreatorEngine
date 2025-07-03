#include "MeshRenderer.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "CullingManager.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "../RenderEngine/Material.h"
#include "../RenderEngine/Mesh.h"
#include "ResourceAllocator.h"

MeshRenderer::MeshRenderer()
{
    m_name = "MeshRenderer"; 
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>();
}

MeshRenderer::~MeshRenderer()
{
}

void MeshRenderer::Awake()
{
    auto scene = SceneManagers->GetActiveScene();
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectMeshRenderer(this);
        renderScene->RegisterCommand(this);
    }

    if(!m_isSkinnedMesh)
    {
        CullingManagers->Insert(this);

		m_isNeedUpdateCulling = true;
    }
}

void MeshRenderer::OnDistroy()
{
    CullingManagers->Remove(this);
	auto scene = SceneManagers->GetActiveScene();
    auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
        renderScene->UnregisterCommand(this);
	}
}

BoundingBox MeshRenderer::GetBoundingBox() const
{
    if (m_Mesh)
    {
        BoundingBox localBoundingBox = m_Mesh->GetBoundingBox();
        auto mat = m_pOwner->m_transform.GetWorldMatrix();
        BoundingBox worldBoundingBox;
        localBoundingBox.Transform(worldBoundingBox, mat);

        return worldBoundingBox;
    }

    return BoundingBox();
}

void MeshRenderer::CullGroupInsert(OctreeNode* node)
{
	if (node)
	{
		m_OctreeNodes.insert(node);
	}
}

void MeshRenderer::CullGroupClear()
{
	m_OctreeNodes.clear();
}
