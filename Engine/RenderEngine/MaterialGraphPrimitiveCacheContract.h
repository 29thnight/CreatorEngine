#pragma once

#include <cstdint>

namespace material_graph
{
    struct ScenePrimitiveCacheContract final
    {
        static constexpr std::uint32_t kCapacity = 16384;
        static constexpr std::uint32_t kProbeCount = 8;
        // 현재 world surface point(80), raster clip(16), 이전 object position(16).
        // 중간 결과를 양자화하지 않아 기존 복원/temporal 계산의 입력 정밀도를 유지한다.
        static constexpr std::uint32_t kVertexBytes = 112;
        static constexpr std::uint32_t kTriangleBytes = 3 * kVertexBytes;
        static constexpr std::uint64_t kKeyBytes = std::uint64_t{kCapacity} * sizeof(std::uint32_t);
        static constexpr std::uint64_t kDataBytes = std::uint64_t{kCapacity} * kTriangleBytes;
        static_assert((kCapacity & (kCapacity - 1)) == 0);
    };
} // namespace material_graph
