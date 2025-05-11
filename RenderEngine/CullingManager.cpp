#include "CullingManager.h"
#include "Core.OctreeNode.h"
#include "MeshRenderer.h"

using namespace DirectX;

CullingManager::~CullingManager()
{
    Clear();
}

void CullingManager::Initialize(const BoundingBox& worldBounds, int maxDepth, int maxMeshesPerNode)
{
    Clear();
    m_maxDepth = maxDepth;
    m_maxMeshesPerNode = maxMeshesPerNode;
    m_root = new OctreeNode(worldBounds, 0);
}

void CullingManager::Insert(MeshRenderer* mesh)
{
    if (m_root)
        m_root->Insert(mesh, m_maxDepth, m_maxMeshesPerNode);
}

void CullingManager::CullMeshes(const BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const
{
    if (m_root)
        CullRecursive(frustum, m_root, outVisibleMeshes);
}

void CullingManager::CullRecursive(const BoundingFrustum& frustum, OctreeNode* node, std::vector<MeshRenderer*>& out) const
{
    ContainmentType result = frustum.Contains(node->boundingBox);

    if (result == DISJOINT)
        return;

    if (result == CONTAINS)
    {
        // 프러스텀에 완전히 포함되면 모든 메쉬 포함
        if (node->isLeaf)
        {
            out.insert(out.end(), node->objects.begin(), node->objects.end());
        }
        else
        {
            for (OctreeNode* child : node->children)
                if (child)
                    CullRecursive(frustum, child, out);
        }
    }
    else // PARTIAL
    {
        if (node->isLeaf)
        {
            for (MeshRenderer* renderer : node->objects)
            {
                if (frustum.Intersects(renderer->GetBoundingBox()))
                    out.push_back(renderer);
            }
        }
        else
        {
            for (OctreeNode* child : node->children)
                if (child)
                    CullRecursive(frustum, child, out);
        }
    }
}

void CullingManager::Clear()
{
    delete m_root;
    m_root = nullptr;
}

void CullingManager::UpdateMesh(MeshRenderer* mesh)
{
    if (!Remove(mesh))
        return;

    Insert(mesh); // 현재 바운딩박스로 재삽입
}

bool CullingManager::Remove(MeshRenderer* mesh)
{
    if (!m_root)
        return false;

    return m_root->Remove(mesh);
}
