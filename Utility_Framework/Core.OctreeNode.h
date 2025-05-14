#pragma once
#include <DirectXCollision.h>
#include <array>
#include <vector>

class MeshRenderer;
class OctreeNode
{
public:
    DirectX::BoundingBox boundingBox;
    std::vector<MeshRenderer*> objects;

    std::array<OctreeNode*, 8> children{};

    int depth = 0;
    bool isLeaf = true;

    OctreeNode(const DirectX::BoundingBox& box, int depth = 0);
    ~OctreeNode();

    void Subdivide(int maxDepth, int maxObjectsPerNode);
    void Insert(MeshRenderer* object, int maxDepth, int maxObjectsPerNode);

    bool Contains(const DirectX::BoundingBox& box) const;
    bool Remove(MeshRenderer* object);

	int GetMaxDepth() const;
};
