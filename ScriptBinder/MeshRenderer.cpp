#include "MeshRenderer.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"

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
