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
    {
		if (child)
		{
			child->~OctreeNode();
			free(child);
		}
    }
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

		void* voidPtr = malloc(sizeof(OctreeNode));
		OctreeNode* child = new (voidPtr) OctreeNode(childBox, depth + 1);

		children[i] = child;
    }
}

void OctreeNode::Insert(MeshRenderer* object, int maxDepth, int maxObjectsPerNode)
{
    if (!boundingBox.Intersects(object->GetBoundingBox()))
        return;

    if (isLeaf)
    {
        objects.push_back(object);
		object->CullGroupInsert(this);

        if (objects.size() > maxObjectsPerNode && depth < maxDepth)
        {
            Subdivide(maxDepth, maxObjectsPerNode);

            std::vector<MeshRenderer*> previousObjects = std::move(objects);
            objects.clear();

            for (MeshRenderer* renderer : previousObjects)
            {
                renderer->CullGroupClear();
                bool inserted = false;

                for (OctreeNode* child : children)
                {
                    if (child && child->boundingBox.Intersects(renderer->GetBoundingBox()))
                    {
                        child->Insert(renderer, maxDepth, maxObjectsPerNode);
                        inserted = true;
                    }
                }

                if (!inserted)
                {
                    objects.push_back(renderer);
                    renderer->CullGroupInsert(this);
                }
            }

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
    bool removed = false;

    if (!boundingBox.Intersects(object->GetBoundingBox()))
        return false;

    if (isLeaf)
    {
        auto it = std::find(objects.begin(), objects.end(), object);
        if (it != objects.end())
        {
            objects.erase(it);
            removed = true;
        }
    }

    for (OctreeNode* child : children)
    {
        if (child)
            removed |= child->Remove(object);
    }

    return removed;
}

bool OctreeNode::RemoveRecursive(MeshRenderer* target)
{
    bool removed = false;

    auto& vec = objects;
    size_t before = vec.size();
    std::erase_if(vec, [&](MeshRenderer* obj) { return obj == target; });
    if (vec.size() < before)
        removed = true;

    for (OctreeNode* child : children)
    {
        if (child)
            removed |= child->RemoveRecursive(target);
    }

    return removed;
}

int OctreeNode::GetMaxDepth() const
{
	if (isLeaf)
		return depth;

	int maxChildDepth = depth;
	for (const OctreeNode* child : children)
	{
		if (child)
			maxChildDepth = std::max(maxChildDepth, child->GetMaxDepth());
	}
	return maxChildDepth;
}
