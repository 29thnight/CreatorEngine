#pragma once
#include "DirectXCollision.h"
#include <vector>
#include <memory>

class Mesh;

class OctreeNode
{
public:
    static constexpr int MaxMeshesPerNode = 10;
    static constexpr float MinNodeSize = 1.0f;

    OctreeNode(const DirectX::BoundingBox& bounds);

    void InsertMesh(Mesh* mesh);
    void FrustumCull(const DirectX::BoundingFrustum& frustum, std::vector<Mesh*>& visibleMeshes) const;

private:
    void Subdivide();
    int GetChildIndexForMesh(const DirectX::BoundingBox& meshBounds) const;

private:
    DirectX::BoundingBox m_bounds;
    std::vector<Mesh*> m_meshes;
    std::unique_ptr<OctreeNode> m_children[8];
    bool m_isLeaf = true;
};