#include "MeshRenderer.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "CullingManager.h"
#include "SceneManager.h"
#include "Scene.h"

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
    if (scene)
    {
        scene->CollectMeshRenderer(this);
    }

    if(!m_isSkinnedMesh)
    {
        CullingManagers->Insert(this);

		m_isNeedUptateCulling = true;
    }
}

void MeshRenderer::OnDistroy()
{
    CullingManagers->Remove(this);
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
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
