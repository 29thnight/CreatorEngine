#pragma once
// CreatorEngine - Core Octree Node (Loose Octree + shared_ptr children, weak_ptr items)

#include <array>
#include <vector>
#include <memory>
#include <DirectXCollision.h>

class MeshRenderer; // fwd

namespace Creator {
    namespace Culling {

        struct OctreeNode : public std::enable_shared_from_this<OctreeNode>
        {
            // Tight bounds: 분할/자식 영역 계산용 (실제 공간 파티션 영역)
            DirectX::BoundingBox                      tightBounds{};

            // Loose bounds: 컬링/레이캐스트/교차 테스트용 (tightBounds * looseFactor)
            DirectX::BoundingBox                      bounds{};

            std::array<std::shared_ptr<OctreeNode>, 8> children{};    // parent -> child shared ownership
            std::vector<std::weak_ptr<MeshRenderer>>   items;         // store items as weak_ptr (no ownership)
            unsigned                                   depth{ 0 };

            inline bool isLeaf() const noexcept {
                for (auto const& c : children) if (c) return false;
                return true;
            }
        };

    }
} // namespace Creator::Culling
