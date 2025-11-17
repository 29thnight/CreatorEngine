#pragma once
// CreatorEngine - Culling Manager (Singleton over DLL boundary)

#include <memory>
#include <vector>
#include <DirectXCollision.h>

#include "Octree.h"
#include "DLLAcrossSingleton.h"

class MeshRenderer;

namespace Creator {
    namespace Culling {

        class CullingManager : public DLLCore::Singleton<CullingManager>
        {
            // Allow Singleton to call our private/protected ctor/dtor
            friend class DLLCore::Singleton<CullingManager>;

        public:
            // Lifecycle
            void Initialize(const DirectX::BoundingBox& world, const OctreeConfig& cfg);
            void Shutdown();

            bool IsInitialized() const noexcept { return static_cast<bool>(m_oct); }

            // Registration / Updates
            void Register(std::shared_ptr<MeshRenderer> mr, std::uint64_t id, const DirectX::BoundingBox& aabb);
            void Unregister(std::uint64_t id);
            void UpdateBounds(std::uint64_t id, const DirectX::BoundingBox& aabb);

            // Queries
            void FrustumCull(const DirectX::BoundingFrustum& fr, std::vector<std::shared_ptr<MeshRenderer>>& out) const;
            void BoxQuery(const DirectX::BoundingBox& area, std::vector<std::shared_ptr<MeshRenderer>>& out) const;

            // Front-to-back 프러스텀 컬링
            void FrustumCullFrontToBack(const DirectX::XMVECTOR& eye,
                const DirectX::BoundingFrustum& fr,
                std::vector<std::shared_ptr<MeshRenderer>>& out) const;

            // Raycast (AABB 기반 1차 테스트)
            void Raycast(const Ray& ray,
                std::vector<std::pair<std::shared_ptr<MeshRenderer>, float>>& hits,
                float maxDistance = FLT_MAX) const;


            // Stats
            [[nodiscard]] Octree::Stats GetStats() const;

        private:
            // ctor/dtor hidden from outside; only Singleton can construct/destroy
            CullingManager() = default;
            ~CullingManager() = default;

        private:
            std::unique_ptr<Octree> m_oct; // thread-safety is handled inside Octree
        };

    }
} // namespace Creator::Culling

static auto CullingManagers = Creator::Culling::CullingManager::GetInstance();