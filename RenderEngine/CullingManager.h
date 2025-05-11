#pragma once
#include <vector>
#include <DirectXCollision.h>

class MeshRenderer;
class OctreeNode;

class CullingManager
{
public:
    CullingManager() = default;
    ~CullingManager();

    void Initialize(const DirectX::BoundingBox& worldBounds, int maxDepth = 5, int maxMeshesPerNode = 10);
    void Insert(MeshRenderer* mesh);
    void CullMeshes(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const;
    bool Remove(MeshRenderer* mesh);
    void UpdateMesh(MeshRenderer* mesh);
    void Clear();

private:
    OctreeNode* m_root = nullptr;
    int m_maxDepth = 5;
    int m_maxMeshesPerNode = 10;

    void CullRecursive(const DirectX::BoundingFrustum& frustum, OctreeNode* node, std::vector<MeshRenderer*>& out) const;
};
