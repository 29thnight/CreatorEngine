#include "MeshRenderer.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "CullingManager.h"

MeshRenderer::MeshRenderer()
{
    m_name = "MeshRenderer"; 
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>();
	CullingManagers->Insert(this);
}

MeshRenderer::~MeshRenderer()
{
	CullingManagers->Remove(this);
}

bool MeshRenderer::IsEnabled() const
{
    return m_IsEnabled;
}

void MeshRenderer::SetEnabled(bool able)
{
    m_IsEnabled = able;
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
