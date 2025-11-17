#include "Octree.h"

using namespace Creator::Culling;
using namespace DirectX;

static inline BoundingBox makeBox(const XMFLOAT3& c, const XMFLOAT3& e) {
    BoundingBox b; b.Center = c; b.Extents = e; return b;
}

Octree::Octree(const BoundingBox& world, const OctreeConfig& cfg)
    : m_worldTight(world), m_cfg(cfg)
{
    InitializeSRWLock(&m_lock);
    m_root = createNode(world, 0);
}


Octree::~Octree()
{
    AcquireSRWLockExclusive(&m_lock);
    m_root.reset();
    m_index.clear();
    m_ptrToId.clear();
    ReleaseSRWLockExclusive(&m_lock);
}

std::shared_ptr<OctreeNode> Octree::createNode(const BoundingBox& tight, unsigned depth)
{
    auto n = std::make_shared<OctreeNode>();
    n->tightBounds = tight;

    // looseFactor 적용
    BoundingBox loose = tight;
    loose.Extents.x *= m_cfg.looseFactor;
    loose.Extents.y *= m_cfg.looseFactor;
    loose.Extents.z *= m_cfg.looseFactor;
    n->bounds = loose;

    n->depth = depth;
    return n;
}


bool Octree::Insert(ObjectID id, const std::shared_ptr<MeshRenderer>& mr, const BoundingBox& aabb)
{
    if (!mr) return false;
    AcquireSRWLockExclusive(&m_lock);

    if (m_index.find(id) != m_index.end()) { ReleaseSRWLockExclusive(&m_lock); return false; }

    const bool ok = insertInternal(m_root, id, mr, aabb);
    if (!ok) {
        m_root->items.push_back(mr);
        m_index.emplace(id, IndexEntry{ id, m_root, mr, mr.get(), aabb });
        m_ptrToId[mr.get()] = id;
    }

    ReleaseSRWLockExclusive(&m_lock);
    return true;
}

bool Octree::insertInternal(const std::shared_ptr<OctreeNode>& n,
    ObjectID id,
    const std::shared_ptr<MeshRenderer>& mr,
    const BoundingBox& box)
{
    if (n->isLeaf()
        && n->depth < m_cfg.maxDepth
        && !anyExtentBelow(n->bounds.Extents, m_cfg.minHalfSize)
        && n->items.size() >= m_cfg.nodeCapacity)
    {
        split(n);
    }

    if (!n->isLeaf()) {
        const int idx = childIndexFor(n.get(), box);
        if (idx >= 0 && fitsInChild(n.get(), box, idx)) {
            return insertInternal(n->children[idx], id, mr, box);
        }
    }

    n->items.push_back(mr);
    m_index.emplace(id, IndexEntry{ id, n, mr, mr.get(), box });
    m_ptrToId[mr.get()] = id;
    return true;
}

bool Octree::Remove(ObjectID id)
{
    AcquireSRWLockExclusive(&m_lock);
    auto it = m_index.find(id);
    if (it == m_index.end()) { ReleaseSRWLockExclusive(&m_lock); return false; }

    auto n = it->second.node.lock();
    auto mr = it->second.mr.lock();
    auto rawPtr = it->second.rawPtr;

    if (n) {
        auto& v = n->items;
        if (mr) {
            v.erase(std::remove_if(v.begin(), v.end(),
                [&](auto const& w) { return sameObject(w, mr); }),
                v.end());
        }
        else {
            pruneExpired(v);
        }
    }

    if (rawPtr) {
        auto pit = m_ptrToId.find(rawPtr);
        if (pit != m_ptrToId.end()) m_ptrToId.erase(pit);
    }
    m_index.erase(it);
    ReleaseSRWLockExclusive(&m_lock);
    return true;
}

bool Octree::Update(ObjectID id, const BoundingBox& newAabb)
{
    AcquireSRWLockExclusive(&m_lock);
    auto it = m_index.find(id);
    if (it == m_index.end()) { ReleaseSRWLockExclusive(&m_lock); return false; }

    auto mr = it->second.mr.lock();
    auto n = it->second.node.lock();
    auto rawPtr = it->second.rawPtr;

    if (n) {
        auto& v = n->items;
        if (mr) {
            v.erase(std::remove_if(v.begin(), v.end(),
                [&](auto const& w) { return sameObject(w, mr); }),
                v.end());
        }
        else {
            pruneExpired(v);
        }
    }
    if (rawPtr) {
        auto pit = m_ptrToId.find(rawPtr);
        if (pit != m_ptrToId.end()) m_ptrToId.erase(pit);
    }
    m_index.erase(it);

    bool ok = false;
    if (mr) {
        ok = insertInternal(m_root, id, mr, newAabb);
    }
    ReleaseSRWLockExclusive(&m_lock);
    return ok;
}

void Octree::split(const std::shared_ptr<OctreeNode>& n)
{
    const XMFLOAT3 c = n->tightBounds.Center;
    const XMFLOAT3 e = n->tightBounds.Extents;
    const XMFLOAT3 he{ e.x * 0.5f, e.y * 0.5f, e.z * 0.5f };

    auto childTight = [&](int idx)->BoundingBox {
        XMFLOAT3 cc{
            c.x + ((idx & 1) ? 0.5f * he.x : -0.5f * he.x),
            c.y + ((idx & 2) ? 0.5f * he.y : -0.5f * he.y),
            c.z + ((idx & 4) ? 0.5f * he.z : -0.5f * he.z)
        };
        return makeBox(cc, he);
        };

    for (int i = 0; i < 8; ++i) n->children[i] = createNode(childTight(i), n->depth + 1);

    std::vector<std::weak_ptr<MeshRenderer>> keep;
    keep.reserve(n->items.size());

    for (auto& wmr : n->items) {
        auto sp = wmr.lock();
        if (!sp) continue;

        // O(1) id lookup via ptr->id cache
        ObjectID id = 0;
        if (auto pit = m_ptrToId.find(sp.get()); pit != m_ptrToId.end()) {
            id = pit->second;
        }
        else {
            // rare fallback
            for (auto const& kv : m_index) {
                if (kv.second.rawPtr == sp.get()) { id = kv.first; break; }
            }
            if (id == 0) { keep.push_back(wmr); continue; }
        }

        const auto eit = m_index.find(id);
        if (eit == m_index.end()) { keep.push_back(wmr); continue; }
        const auto& box = eit->second.box;

        const int idx = childIndexFor(n.get(), box);
        if (idx >= 0 && fitsInChild(n.get(), box, idx)) {
            insertInternal(n->children[idx], id, sp, box);
        }
        else {
            keep.push_back(wmr);
        }
    }
    n->items.swap(keep);
}

inline bool Octree::anyExtentBelow(const XMFLOAT3& e, float minHalf) {
    return (e.x < minHalf) || (e.y < minHalf) || (e.z < minHalf);
}

int Octree::childIndexFor(const OctreeNode* n, const BoundingBox& box) const
{
    const XMFLOAT3 c = n->tightBounds.Center;
    const XMFLOAT3 bc = box.Center;
    int idx = 0;
    idx |= (bc.x >= c.x) ? 1 : 0;
    idx |= (bc.y >= c.y) ? 2 : 0;
    idx |= (bc.z >= c.z) ? 4 : 0;
    return idx;
}

bool Octree::fitsInChild(const OctreeNode* n, const BoundingBox& box, int childIdx) const
{
    // child loose bounds 사용
    const auto& child = n->children[childIdx];
    if (!child) return false;
    return child->bounds.Contains(box) == ContainmentType::CONTAINS;
}

void Octree::Cull(const BoundingFrustum& fr, std::vector<std::shared_ptr<MeshRenderer>>& outVisible) const
{
    outVisible.clear();
    AcquireSRWLockShared(&m_lock);
    cullFrustum(m_root, fr, outVisible);
    ReleaseSRWLockShared(&m_lock);
}

void Octree::cullFrustum(const std::shared_ptr<OctreeNode>& n,
    const BoundingFrustum& fr,
    std::vector<std::shared_ptr<MeshRenderer>>& out) const
{
    if (!n) return;

    const auto ct = fr.Contains(n->bounds);
    if (ct == ContainmentType::DISJOINT) return;

    if (ct == ContainmentType::CONTAINS) {
        std::vector<std::shared_ptr<OctreeNode>> st{ n };
        while (!st.empty()) {
            auto cur = std::move(st.back()); st.pop_back();

            for (auto it = cur->items.begin(); it != cur->items.end();) {
                if (it->expired()) { it = cur->items.erase(it); continue; }
                auto sp = it->lock();
                if (!sp) { it = cur->items.erase(it); continue; }

                // full contain ⇒ no per-item test required to push
                out.push_back(sp);
                ++it;
            }
            for (auto const& c : cur->children) if (c) st.push_back(c);
        }
        return;
    }

    // partial
    for (auto it = n->items.begin(); it != n->items.end();) {
        if (it->expired()) { it = n->items.erase(it); continue; }
        auto sp = it->lock();
        if (!sp) { it = n->items.erase(it); continue; }

        if (auto pit = m_ptrToId.find(sp.get()); pit != m_ptrToId.end()) {
            if (auto eit = m_index.find(pit->second); eit != m_index.end()) {
                if (fr.Intersects(eit->second.box)) out.push_back(sp);
            }
        }
        ++it;
    }

    for (auto const& c : n->children) if (c) cullFrustum(c, fr, out);
}

void Octree::Cull(const BoundingBox& area, std::vector<std::shared_ptr<MeshRenderer>>& outOverlaps) const
{
    outOverlaps.clear();
    AcquireSRWLockShared(&m_lock);
    cullBox(m_root, area, outOverlaps);
    ReleaseSRWLockShared(&m_lock);
}

void Octree::cullBox(const std::shared_ptr<OctreeNode>& n,
    const BoundingBox& area,
    std::vector<std::shared_ptr<MeshRenderer>>& out) const
{
    if (!n || !n->bounds.Intersects(area)) return;

    for (auto it = n->items.begin(); it != n->items.end();) {
        if (it->expired()) { it = n->items.erase(it); continue; }
        auto sp = it->lock();
        if (!sp) { it = n->items.erase(it); continue; }

        if (auto pit = m_ptrToId.find(sp.get()); pit != m_ptrToId.end()) {
            if (auto eit = m_index.find(pit->second); eit != m_index.end()) {
                if (eit->second.box.Intersects(area)) out.push_back(sp);
            }
        }
        ++it;
    }

    for (auto const& c : n->children) if (c) cullBox(c, area, out);
}

void Octree::Clear()
{
    AcquireSRWLockExclusive(&m_lock);

    // 기존 노드/인덱스/캐시 정리
    m_root.reset();
    m_index.clear();
    m_ptrToId.clear();

    // 루트 다시 생성: tight = m_worldTight, 내부에서 looseFactor 적용
    m_root = createNode(m_worldTight, 0);

    ReleaseSRWLockExclusive(&m_lock);
}

BoundingBox Octree::Bounds() const
{
    AcquireSRWLockShared(&m_lock);

    BoundingBox b{};
    if (m_root) {
        // 루트의 loose bounds 를 반환 (실제 컬링/레이캐스트에 쓰이는 영역)
        b = m_root->bounds;
        // 만약 tight 기준의 월드 AABB가 필요하면 이 줄 대신:
        // b = m_root->tightBounds;
    }

    ReleaseSRWLockShared(&m_lock);
    return b;
}

Octree::Stats Octree::GetStats() const
{
    AcquireSRWLockShared(&m_lock);
    Stats s{};
    std::vector<std::shared_ptr<OctreeNode>> st;
    if (m_root) st.push_back(m_root);
    while (!st.empty()) {
        auto n = std::move(st.back()); st.pop_back();
        ++s.nodeCount;
        s.itemCount += static_cast<std::uint32_t>(n->items.size());
        s.depth = std::max(s.depth, n->depth);
        for (auto const& c : n->children) if (c) st.push_back(c);
    }
    ReleaseSRWLockShared(&m_lock);
    return s;
}

void Octree::Raycast(const Ray& ray,
    std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
    float maxDistance) const
{
    hits.clear();
    AcquireSRWLockShared(&m_lock);
    if (m_root) {
        raycastNode(m_root, ray, hits, maxDistance);
    }
    ReleaseSRWLockShared(&m_lock);

    // 거리 기준으로 정렬
    std::sort(hits.begin(), hits.end(),
        [](auto const& a, auto const& b) { return a.second < b.second; });
}

void Octree::raycastNode(const std::shared_ptr<OctreeNode>& n,
    const Ray& ray,
    std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
    float maxDistance) const
{
    if (!n) return;

    // 노드 loose bounds와 레이 교차 테스트
    float nodeDist = 0.0f;
    if (!n->bounds.Intersects(ray.origin, ray.dir, nodeDist)) // DirectX::BoundingBox::Intersects(origin,dir,dist)
        return;
    if (nodeDist > maxDistance) return;

    // 아이템들 Ray-AABB 테스트
    for (auto it = n->items.begin(); it != n->items.end();) {
        if (it->expired()) { it = n->items.erase(it); continue; }
        auto sp = it->lock();
        if (!sp) { it = n->items.erase(it); continue; }

        // 인덱스에서 AABB 가져오기
        float dist = FLT_MAX;
        if (auto pit = m_ptrToId.find(sp.get()); pit != m_ptrToId.end()) {
            if (auto eit = m_index.find(pit->second); eit != m_index.end()) {
                const auto& box = eit->second.box;
                if (box.Intersects(ray.origin, ray.dir, dist) && dist <= maxDistance) {
                    hits.emplace_back(sp, dist);
                }
            }
        }
        ++it;
    }

    // 자식들 탐색 (더 정교하게 하려면 child와의 intersection dist 기준으로 정렬된 우선순위 큐 사용 가능)
    if (!n->isLeaf()) {
        for (auto const& c : n->children) {
            if (c) raycastNode(c, ray, hits, maxDistance);
        }
    }
}


void Octree::collectFrustumFrontToBack(const std::shared_ptr<OctreeNode>& n,
    const XMVECTOR& eye,
    const BoundingFrustum& fr,
    std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& out) const
{
    if (!n) return;

    const auto ct = fr.Contains(n->bounds);
    if (ct == ContainmentType::DISJOINT) return;

    // 노드 중심과 eye 거리
    XMVECTOR center = XMLoadFloat3(&n->bounds.Center);
    float nodeDist = XMVectorGetX(XMVector3Length(center - eye));

    // 부분/전체 둘 다 아이템 검사 (전체일 때도 오클루전용 정렬이 필요하니 거리 계산)
    for (auto it = n->items.begin(); it != n->items.end();) {
        if (it->expired()) { it = n->items.erase(it); continue; }
        auto sp = it->lock();
        if (!sp) { it = n->items.erase(it); continue; }

        float dist = nodeDist;
        // 더 정교하게 하려면 인덱스에서 AABB center를 읽어 거리 재계산해도 됨

        out.emplace_back(sp, dist);
        ++it;
    }

    if (!n->isLeaf()) {
        // 자식들도 재귀
        for (auto const& c : n->children) {
            if (c) collectFrustumFrontToBack(c, eye, fr, out);
        }
    }
}


void Octree::FrustumCullFrontToBack(const XMVECTOR& eye,
    const BoundingFrustum& fr,
    std::vector<std::shared_ptr<MeshRenderer>>& outVisible) const
{
    outVisible.clear();
    AcquireSRWLockShared(&m_lock);

    std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>> tmp;
    if (m_root) {
        collectFrustumFrontToBack(m_root, eye, fr, tmp);
    }

    ReleaseSRWLockShared(&m_lock);

    // 거리 기준 정렬
    std::sort(tmp.begin(), tmp.end(),
        [](auto const& a, auto const& b) { return a.second < b.second; });

    outVisible.reserve(tmp.size());
    for (auto& p : tmp) outVisible.push_back(std::move(p.first));
}
