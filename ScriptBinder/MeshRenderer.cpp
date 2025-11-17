#include "MeshRenderer.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "CullingManager.h"
#include "SceneManager.h"
#include "RenderScene.h"
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
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectMeshRenderer(this);
        renderScene->RegisterCommand(this);
    }

    if(!m_isSkinnedMesh)
    {
		HashedGuid instanceID = GetInstanceID();
        CullingManagers->Register(shared_from_this(), instanceID.m_ID_Data, GetBoundingBox());

		m_isNeedUpdateCulling = true;
    }
}

void MeshRenderer::OnDestroy()
{
    //CullingManagers->Remove(this);
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
        renderScene->UnregisterCommand(this);
	}

    if (!m_isSkinnedMesh)
    {
        HashedGuid instanceID = GetInstanceID();
        CullingManagers->Unregister(instanceID.m_ID_Data);
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