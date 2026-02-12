#include "CullingManager.h"

using namespace Creator::Culling;
using namespace DirectX;

void CullingManager::Initialize(const BoundingBox& world, const OctreeConfig& cfg)
{
    if (!m_oct) {
        m_oct = std::make_unique<Octree>(world, cfg);
    }
}

void CullingManager::Shutdown()
{
    m_oct.reset();
}

void CullingManager::Register(std::shared_ptr<MeshRenderer> mr, std::uint64_t id, const BoundingBox& aabb)
{
    if (!m_oct || !mr) return;
    m_oct->Insert(id, mr, aabb);
}

void CullingManager::Unregister(std::uint64_t id)
{
    if (!m_oct) return;
    m_oct->Remove(id);
}

void CullingManager::UpdateBounds(std::uint64_t id, const BoundingBox& aabb)
{
    if (!m_oct) return;
    m_oct->Update(id, aabb);
}

void CullingManager::FrustumCull(const BoundingFrustum& fr, std::vector<std::shared_ptr<MeshRenderer>>& out) const
{
    out.clear();
    if (!m_oct) return;
    m_oct->Cull(fr, out);
}

void CullingManager::BoxQuery(const BoundingBox& area, std::vector<std::shared_ptr<MeshRenderer>>& out) const
{
    out.clear();
    if (!m_oct) return;
    m_oct->Cull(area, out);
}

Octree::Stats CullingManager::GetStats() const
{
    if (!m_oct) return {};
    return m_oct->GetStats();
}

void CullingManager::FrustumCullFrontToBack(const XMVECTOR& eye,
    const BoundingFrustum& fr,
    std::vector<std::shared_ptr<MeshRenderer>>& out) const
{
    out.clear();
    if (!m_oct) return;
    m_oct->FrustumCullFrontToBack(eye, fr, out);
}

void CullingManager::Raycast(const Ray& ray,
    std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
    float maxDistance) const
{
    hits.clear();
    if (!m_oct) return;
    m_oct->Raycast(ray, hits, maxDistance);
}
