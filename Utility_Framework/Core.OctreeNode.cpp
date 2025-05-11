#include "CullingManager.h"
#include "Core.OctreeNode.h"
#include "MeshRenderer.h"

using namespace DirectX;

inline OctreeNode::OctreeNode(const DirectX::BoundingBox& box, int depth) :
    boundingBox(box), depth(depth), isLeaf(true)
{
    children.fill(nullptr);
}

OctreeNode::~OctreeNode()
{
    for (OctreeNode* child : children)
        delete child;
}

void OctreeNode::Subdivide(int maxDepth, int maxObjectsPerNode)
{
    if (depth >= maxDepth)
        return;

    isLeaf = false;

    const XMFLOAT3& c = boundingBox.Center;
    const XMFLOAT3& e = boundingBox.Extents;

    XMFLOAT3 childExtents = { e.x * 0.5f, e.y * 0.5f, e.z * 0.5f };

    for (int i = 0; i < 8; ++i)
    {
        float dx = ((i & 1) ? 0.5f : -0.5f) * e.x;
        float dy = ((i & 2) ? 0.5f : -0.5f) * e.y;
        float dz = ((i & 4) ? 0.5f : -0.5f) * e.z;

        BoundingBox childBox;
        childBox.Center = XMFLOAT3(c.x + dx, c.y + dy, c.z + dz);
        childBox.Extents = childExtents;

        children[i] = new OctreeNode(childBox, depth + 1);
    }
}

void OctreeNode::Insert(MeshRenderer* object, int maxDepth, int maxObjectsPerNode)
{
    if (!boundingBox.Intersects(object->GetBoundingBox()))
        return;

    if (isLeaf)
    {
        objects.push_back(object);

        if (objects.size() > maxObjectsPerNode && depth < maxDepth)
        {
            Subdivide(maxDepth, maxObjectsPerNode);

            // 재분배
            for (MeshRenderer* m : objects)
            {
                for (OctreeNode* child : children)
                    if (child) child->Insert(m, maxDepth, maxObjectsPerNode);
            }

            objects.clear();
        }
    }
    else
    {
        for (OctreeNode* child : children)
            if (child)
                child->Insert(object, maxDepth, maxObjectsPerNode);
    }
}

bool OctreeNode::Contains(const DirectX::BoundingBox& box) const
{
    return boundingBox.Contains(box) != ContainmentType::DISJOINT;
}

bool OctreeNode::Remove(MeshRenderer* object)
{
    if (!boundingBox.Intersects(object->GetBoundingBox()))
        return false;

    if (isLeaf)
    {
        auto it = std::find(objects.begin(), objects.end(), object);
        if (it != objects.end())
        {
            objects.erase(it);
            return true;
        }
        return false;
    }

    for (OctreeNode* child : children)
    {
        if (child && child->Remove(object))
            return true;
    }

    return false;
}
