#include "VertexCacheOptimization.h"

#include "meshoptimizer.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // ★ 이름에 패스를 박았다. MSVC 유니티 빌드는 **같은 namespace 의
        //   익명 namespace 를 합친다** — VertexWelding.cpp 에 같은 이름의
        //   헬퍼가 있어 "이미 본문이 있습니다" 로 바로 깨졌다.
        //   익명 namespace 라고 파일 안에서 고립된다고 생각하면 안 된다.
        [[nodiscard]] double CacheOptElapsedMs(Clock::time_point since) noexcept
        {
            return std::chrono::duration<double, std::milli>(
                Clock::now() - since).count();
        }

        // 분석에 쓸 가상 GPU 파라미터. meshoptimizer 문서가 쓰는 표준값이고,
        // **절대값이 아니라 전후 비교**가 목적이므로 무엇을 쓰든 일관되면 된다.
        inline constexpr unsigned int kCacheSize = 16;
        inline constexpr unsigned int kWarpSize = 0;
        inline constexpr unsigned int kPrimGroupSize = 0;

        // overfetch 는 정점 크기를 알아야 계산된다. IR 은 SoA 라 "한 정점의
        // 바이트"가 스트림 존재 여부로 달라지므로 실제로 들고 있는 것만 더한다.
        [[nodiscard]] std::size_t VertexBytes(const VertexStreams& streams) noexcept
        {
            std::size_t bytes = 0;
            if (!streams.positions.empty()) bytes += sizeof(math::vector3);
            if (!streams.normals.empty())   bytes += sizeof(math::vector3);
            if (!streams.uv0.empty())       bytes += sizeof(math::vector2);
            if (!streams.uv1.empty())       bytes += sizeof(math::vector2);
            if (!streams.tangents.empty())  bytes += sizeof(math::vector4);
            if (!streams.colors.empty())    bytes += sizeof(math::vector4);
            return bytes;
        }
    }

    VertexCacheStats OptimizeVertexCache(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes)
    {
        VertexCacheStats stats;
        if (!options.optimizeVertexCache) return stats;

        const auto begin = Clock::now();

        // 가중 평균을 위한 누적. 메시마다 삼각형 수가 크게 다르므로 단순
        // 평균을 내면 작은 메시가 결과를 지배한다.
        double triangleTotal = 0.0;
        double acmrBeforeSum = 0.0, acmrAfterSum = 0.0;
        double vertexBytesTotal = 0.0;
        double fetchBeforeSum = 0.0, fetchAfterSum = 0.0;

        for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex)
        {
            ImportedMesh& mesh = scene.meshes[meshIndex];
            const std::size_t vertexCount = mesh.streams.positions.size();
            const std::size_t indexCount = mesh.indices.size();
            const std::string context = "mesh[" + std::to_string(meshIndex) + "]";

            if (0 == vertexCount || 0 == indexCount)
            {
                ++stats.meshesSkipped;
                continue;
            }
            // meshoptimizer 는 삼각형 리스트를 전제한다. 3의 배수가 아니면
            // 마지막 조각을 어떻게 다룰지 규약이 없으므로 손대지 않는다.
            if (0 != indexCount % 3)
            {
                notes.Warn(ImportNoteCode::InvalidSceneStructure, context,
                    "인덱스 수가 3의 배수가 아니다 — 캐시 최적화를 건너뛴다.");
                ++stats.meshesSkipped;
                continue;
            }

            stats.verticesBefore += vertexCount;
            const std::size_t triangles = indexCount / 3;
            const std::size_t vertexBytes = VertexBytes(mesh.streams);

            const meshopt_VertexCacheStatistics cacheBefore =
                meshopt_analyzeVertexCache(mesh.indices.data(), indexCount,
                    vertexCount, kCacheSize, kWarpSize, kPrimGroupSize);
            const meshopt_VertexFetchStatistics fetchBefore =
                meshopt_analyzeVertexFetch(mesh.indices.data(), indexCount,
                    vertexCount, vertexBytes);

            // ── 1. 삼각형 순서 (인덱스만 바뀐다) ────────────────────────
            std::vector<std::uint32_t> optimized(indexCount);
            meshopt_optimizeVertexCache(optimized.data(), mesh.indices.data(),
                indexCount, vertexCount);

            // ── 2. 정점 순서 remap ──────────────────────────────────────
            // remap[old] = new. 참조되지 않는 정점은 ~0u 가 되어 사라진다.
            std::vector<std::uint32_t> remap(vertexCount);
            const std::size_t unique = meshopt_optimizeVertexFetchRemap(
                remap.data(), optimized.data(), indexCount, vertexCount);

            if (unique > vertexCount)
            {
                // 일어나면 안 되지만, 일어났는데 그대로 쓰면 범위 밖을 쓴다.
                notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                    "remap 결과가 원본보다 정점이 많다 — 최적화를 건너뛴다.");
                ++stats.meshesSkipped;
                stats.verticesBefore -= vertexCount;
                continue;
            }

            // ── 3. 스트림 재배치 ────────────────────────────────────────
            // SoA 라 meshopt_remapVertexBuffer 를 스트림마다 부를 수도 있지만,
            // skin 은 정점당 가변 길이라 그 API 로는 못 옮긴다. 그래서 새
            // 인덱스 순서로 한 번 훑으며 ValueStreams()/AppendSkin 규약을
            // 그대로 쓴다 — 새 스트림이 생겨도 자동으로 따라온다.
            std::vector<std::uint32_t> inverse(unique, ~0u);
            for (std::uint32_t original = 0; original < vertexCount; ++original)
            {
                const std::uint32_t fresh = remap[original];
                if (fresh == ~0u) continue;            // 참조되지 않는 정점
                if (fresh < unique) inverse[fresh] = original;
            }

            bool inverseComplete = true;
            for (const std::uint32_t original : inverse)
                if (original == ~0u) { inverseComplete = false; break; }

            if (!inverseComplete)
            {
                // remap 이 전사가 아니면 어떤 새 정점이 원본을 못 찾는다.
                // 조용히 빈 정점을 만드느니 최적화를 포기한다.
                notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                    "remap 이 새 정점 일부를 채우지 못했다 — 최적화를 건너뛴다.");
                ++stats.meshesSkipped;
                stats.verticesBefore -= vertexCount;
                continue;
            }

            VertexStreams reordered;
            reordered.positions.reserve(unique);
            if (mesh.streams.HasSkin())
            {
                reordered.influenceOffsets.push_back(0);
                reordered.influences.reserve(mesh.streams.influences.size());
            }
            for (const std::uint32_t original : inverse)
            {
                AppendValueStreams(mesh.streams, original, reordered);
                AppendSkin(mesh.streams, original, reordered);
            }

            for (std::uint32_t& index : optimized) index = remap[index];

            mesh.streams = std::move(reordered);
            mesh.indices = std::move(optimized);

            const meshopt_VertexCacheStatistics cacheAfter =
                meshopt_analyzeVertexCache(mesh.indices.data(), indexCount,
                    unique, kCacheSize, kWarpSize, kPrimGroupSize);
            const meshopt_VertexFetchStatistics fetchAfter =
                meshopt_analyzeVertexFetch(mesh.indices.data(), indexCount,
                    unique, vertexBytes);

            triangleTotal += static_cast<double>(triangles);
            acmrBeforeSum += cacheBefore.acmr * static_cast<double>(triangles);
            acmrAfterSum += cacheAfter.acmr * static_cast<double>(triangles);

            const double weight = static_cast<double>(unique * vertexBytes);
            vertexBytesTotal += weight;
            fetchBeforeSum += fetchBefore.overfetch * weight;
            fetchAfterSum += fetchAfter.overfetch * weight;

            stats.verticesAfter += unique;
            ++stats.meshesProcessed;

            if (unique < vertexCount)
            {
                notes.Info(ImportNoteCode::UnreferencedVerticesDropped, context,
                    "인덱스가 참조하지 않는 정점 "
                    + std::to_string(vertexCount - unique) + "개를 버렸다.");
            }
        }

        if (triangleTotal > 0.0)
        {
            stats.acmrBefore = acmrBeforeSum / triangleTotal;
            stats.acmrAfter = acmrAfterSum / triangleTotal;
        }
        if (vertexBytesTotal > 0.0)
        {
            stats.overfetchBefore = fetchBeforeSum / vertexBytesTotal;
            stats.overfetchAfter = fetchAfterSum / vertexBytesTotal;
        }
        stats.optimizeMs = CacheOptElapsedMs(begin);
        return stats;
    }
}
