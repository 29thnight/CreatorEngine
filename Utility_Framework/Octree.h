#pragma once
// CreatorEngine - Thread-safe Loose Octree for MeshRenderer culling
// - Loose-Octree: node.bounds = node.tightBounds * looseFactor
// - Nodes: shared_ptr / weak_ptr
// - Items: weak_ptr<MeshRenderer>
// - SRWLOCK (multi-reader / single-writer)
// - Optimizations:
//     * ptr->id cache: std::unordered_map<MeshRenderer*, ObjectID>
//     * IndexEntry stores id + rawPtr + box
// - Extra features:
//     * Raycast
//     * Front-to-back frustum cull for occlusion pipelines

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>
#include <algorithm>
#include <cfloat>

#define NOMINMAX
#include <windows.h>
#include <DirectXCollision.h>

#include "Core.OctreeNode.h"

class MeshRenderer;

namespace Creator {
    namespace Culling {

        using ObjectID = std::uint64_t;

        // 간단한 Ray 구조체 (DXMath vector 사용)
        struct Ray
        {
            DirectX::XMVECTOR origin; // world-space
            DirectX::XMVECTOR dir;    // normalized
        };

        struct OctreeConfig {
            std::uint32_t nodeCapacity = 16;
            std::uint32_t maxDepth = 10;
            float         minHalfSize = 0.25f;

            // Loose-Octree 계수: 1.0f = 일반 Octree, 2.0f = 각 노드 extents 두 배
            float         looseFactor = 1.0f;
        };

        class Octree
        {
        public:
            explicit Octree(const DirectX::BoundingBox& world, const OctreeConfig& cfg = {});
            ~Octree();

            Octree(const Octree&) = delete;
            Octree& operator=(const Octree&) = delete;

            bool Insert(ObjectID id, const std::shared_ptr<MeshRenderer>& mr, const DirectX::BoundingBox& aabb);
            bool Remove(ObjectID id);
            bool Update(ObjectID id, const DirectX::BoundingBox& newAabb);

            // === 프러스텀 / 박스 컬링 ===
            void Cull(const DirectX::BoundingFrustum& fr,
                std::vector<std::shared_ptr<MeshRenderer>>& outVisible) const;

            void Cull(const DirectX::BoundingBox& area,
                std::vector<std::shared_ptr<MeshRenderer>>& outOverlaps) const;

            // === Front-to-back 프러스텀 컬링 ===
            //   - eye: 카메라 위치
            //   - outVisible: 프러스텀 내의 객체를 eye와의 거리 오름차순으로 정렬해서 반환
            void FrustumCullFrontToBack(const DirectX::XMVECTOR& eye,
                const DirectX::BoundingFrustum& fr,
                std::vector<std::shared_ptr<MeshRenderer>>& outVisible) const;

            // === Raycast ===
            //   - maxDistance: Ray origin에서의 최대 거리
            //   - hits: (renderer, distance) 리스트, distance 오름차순 정렬되어 반환
            void Raycast(const Ray& ray,
                std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
                float maxDistance = FLT_MAX) const;

            void Clear();
            DirectX::BoundingBox Bounds() const;

            struct Stats { std::uint32_t nodeCount{ 0 }, itemCount{ 0 }, depth{ 0 }; };
            [[nodiscard]] Stats GetStats() const;

        private:
            struct IndexEntry {
                ObjectID                        id{ 0 };
                std::weak_ptr<OctreeNode>       node;
                std::weak_ptr<MeshRenderer>     mr;
                MeshRenderer* rawPtr{ nullptr };
                DirectX::BoundingBox            box{};
            };

            std::shared_ptr<OctreeNode> createNode(const DirectX::BoundingBox& tight, unsigned depth);
            void                        split(const std::shared_ptr<OctreeNode>& n);

            bool insertInternal(const std::shared_ptr<OctreeNode>& n,
                ObjectID id,
                const std::shared_ptr<MeshRenderer>& mr,
                const DirectX::BoundingBox& box);

            void cullFrustum(const std::shared_ptr<OctreeNode>& n,
                const DirectX::BoundingFrustum& fr,
                std::vector<std::shared_ptr<MeshRenderer>>& out) const;

            void cullBox(const std::shared_ptr<OctreeNode>& n,
                const DirectX::BoundingBox& area,
                std::vector<std::shared_ptr<MeshRenderer>>& out) const;

            // Front-to-back용 내부 수집 (거리 계산만)
            void collectFrustumFrontToBack(const std::shared_ptr<OctreeNode>& n,
                const DirectX::XMVECTOR& eye,
                const DirectX::BoundingFrustum& fr,
                std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& out) const;

            // Raycast 내부 구현
            void raycastNode(const std::shared_ptr<OctreeNode>& n,
                const Ray& ray,
                std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
                float maxDistance) const;

            static inline DirectX::BoundingBox makeChildTightBounds(const DirectX::BoundingBox& parentTight, int idx);
            static inline bool anyExtentBelow(const DirectX::XMFLOAT3& e, float minHalf);
            int  childIndexFor(const OctreeNode* n, const DirectX::BoundingBox& box) const;
            bool fitsInChild(const OctreeNode* n, const DirectX::BoundingBox& box, int childIdx) const;

            static inline bool sameObject(const std::weak_ptr<MeshRenderer>& w,
                const std::shared_ptr<MeshRenderer>& s) {
                if (w.expired() || !s) return false;
                return w.lock().get() == s.get();
            }
            static inline void pruneExpired(std::vector<std::weak_ptr<MeshRenderer>>& v) {
                v.erase(std::remove_if(v.begin(), v.end(),
                    [](auto const& w) { return w.expired(); }), v.end());
            }

        private:
            std::shared_ptr<OctreeNode>                 m_root;
            DirectX::BoundingBox                        m_worldTight{};   // 루트 tight
            OctreeConfig                                m_cfg{};

            std::unordered_map<ObjectID, IndexEntry>    m_index;
            std::unordered_map<MeshRenderer*, ObjectID> m_ptrToId;

            mutable SRWLOCK                             m_lock{};
        };

    }
} // namespace Creator::Culling
