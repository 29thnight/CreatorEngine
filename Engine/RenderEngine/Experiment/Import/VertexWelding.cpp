#include "VertexWelding.h"

#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        [[nodiscard]] double ElapsedMs(Clock::time_point since) noexcept
        {
            return std::chrono::duration<double, std::milli>(
                Clock::now() - since).count();
        }

        // ★ 비트 정확 비교다. epsilon 을 두지 않는다.
        //
        //   붙이면 안 되는 이음매를 붙이는 쪽이, 붙일 수 있었는데 안 붙이는
        //   쪽보다 훨씬 나쁘다. 전자는 화면에 조명 이음매로 나타나고 원인을
        //   찾기 어렵다. 후자는 정점이 몇 개 더 남을 뿐이다.
        //
        //   -0.0 과 +0.0 만 같은 값으로 본다. 부호만 다른 0 이 정점을 쓸데없이
        //   쪼개면 용접이 제 일을 못 한다(탄젠트 재용접이 같은 이유로 같은
        //   처리를 한다).
        [[nodiscard]] std::uint32_t FloatBits(float value) noexcept
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            if (bits == 0x80000000u) bits = 0u;
            return bits;
        }

        void HashCombine(std::size_t& hash, std::uint32_t value) noexcept
        {
            hash ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ULL
                + (hash << 6) + (hash >> 2);
        }

        // 정점 하나의 키. 값 스트림 전부 + skin influence 를 바이트로 편다.
        //
        // ★ 키를 손으로 나열하지 않는다. ValueStreams() 를 순회해 만들기 때문에
        //   새 스트림이 생기면 **복사에도 키에도** 자동으로 들어온다. 둘 중
        //   하나만 따라오면(예: 복사만) 그 속성만 다른 두 정점이 합쳐지면서
        //   조용히 값이 사라진다 — 목록이 정본인 이유가 이것이다.
        struct VertexKey final
        {
            std::vector<std::uint32_t> words{};

            [[nodiscard]] bool operator==(const VertexKey& other) const noexcept
            {
                return words == other.words;
            }
        };

        struct VertexKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const VertexKey& key) const noexcept
            {
                std::size_t hash = 1469598103934665603ULL;
                for (const std::uint32_t word : key.words) HashCombine(hash, word);
                return hash;
            }
        };

        void AppendFloats(VertexKey& key, const float* first, std::size_t count)
        {
            for (std::size_t i = 0; i < count; ++i)
                key.words.push_back(FloatBits(first[i]));
        }

        [[nodiscard]] VertexKey MakeKey(const VertexStreams& streams, std::uint32_t vertex)
        {
            VertexKey key;
            std::apply([&](auto... members)
            {
                const auto append = [&](auto member)
                {
                    const auto& stream = streams.*member;
                    if (stream.empty()) return;
                    using Element = std::decay_t<decltype(stream[0])>;
                    static_assert(std::is_trivially_copyable_v<Element>);
                    // math::vector2/3/4 는 전부 packed float 이라 첫 필드부터
                    // 연속이다(라이브러리가 static_assert 로 고정한다).
                    const Element& value = stream[vertex];
                    AppendFloats(key, reinterpret_cast<const float*>(&value),
                        sizeof(Element) / sizeof(float));
                };
                (append(members), ...);
            }, VertexStreams::ValueStreams());

            // skin 은 정점당 가변 길이라 목록 밖이다(ImportedScene.h 의 규약).
            // 길이도 키에 넣는다 — 넣지 않으면 influence 가 하나 더 있는 정점이
            // 접두사가 같다는 이유로 합쳐질 수 있다.
            if (streams.HasSkin())
            {
                const std::span<const JointInfluence> influences =
                    streams.InfluencesOf(vertex);
                key.words.push_back(static_cast<std::uint32_t>(influences.size()));
                for (const JointInfluence& influence : influences)
                {
                    key.words.push_back(influence.joint.Value());
                    key.words.push_back(FloatBits(influence.weight));
                }
            }
            return key;
        }
    }

    VertexWeldStats WeldVertices(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes)
    {
        VertexWeldStats stats;
        if (!options.weldVertices) return stats;

        const auto begin = Clock::now();

        for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex)
        {
            ImportedMesh& mesh = scene.meshes[meshIndex];
            const std::size_t vertexCount = mesh.streams.positions.size();

            // 인덱스가 없으면 정점 순서 자체가 의미이므로 건드리지 않는다.
            // 합치면 삼각형 구성이 무너진다.
            if (0 == vertexCount || mesh.indices.empty())
            {
                ++stats.meshesSkipped;
                continue;
            }

            const std::string context = "mesh[" + std::to_string(meshIndex) + "]";
            stats.verticesBefore += vertexCount;

            std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> lookup;
            lookup.reserve(vertexCount);

            VertexStreams welded;
            welded.positions.reserve(vertexCount);
            if (mesh.streams.HasSkin())
            {
                welded.influenceOffsets.push_back(0);
                welded.influences.reserve(mesh.streams.influences.size());
            }

            // 원본 정점 -> 새 정점. 인덱스 순서대로 처음 만난 것에 번호를 주므로
            // 결과가 결정적이다(같은 입력이면 같은 바이트가 나온다).
            std::vector<std::uint32_t> remap(vertexCount);
            for (std::uint32_t original = 0; original < vertexCount; ++original)
            {
                VertexKey key = MakeKey(mesh.streams, original);
                const auto found = lookup.find(key);
                if (found != lookup.end())
                {
                    remap[original] = found->second;
                    continue;
                }
                const auto fresh = static_cast<std::uint32_t>(welded.positions.size());
                AppendValueStreams(mesh.streams, original, welded);
                AppendSkin(mesh.streams, original, welded);
                lookup.emplace(std::move(key), fresh);
                remap[original] = fresh;
            }

            const std::size_t after = welded.positions.size();

            // ★ 용접은 정점을 늘릴 수 없다. 늘었다면 remap 이 깨진 것이므로
            //   결과를 쓰지 않고 원본을 남긴다 — 조용히 잘못된 메시를 내보내는
            //   것보다 용접을 포기하는 쪽이 낫다.
            if (after > vertexCount)
            {
                notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                    "용접 결과가 원본보다 정점이 많다 — 용접을 건너뛴다.");
                ++stats.meshesSkipped;
                stats.verticesBefore -= vertexCount;
                continue;
            }

            for (std::uint32_t& index : mesh.indices)
            {
                if (index >= vertexCount)
                {
                    // 임포터가 이미 검증하지만, 여기서 remap 을 태우면 범위 밖
                    // 읽기가 되므로 한 번 더 막는다.
                    notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                        "인덱스가 정점 수를 넘는다 — 용접을 건너뛴다.");
                    index = 0;
                    continue;
                }
                index = remap[index];
            }

            mesh.streams = std::move(welded);
            stats.verticesAfter += after;
            ++stats.meshesProcessed;

            if (after < vertexCount)
            {
                notes.Info(ImportNoteCode::VerticesWelded, context,
                    "동일 정점 " + std::to_string(vertexCount - after)
                    + "개를 합쳤다(" + std::to_string(vertexCount) + " -> "
                    + std::to_string(after) + ").");
            }
        }

        stats.weldMs = ElapsedMs(begin);
        return stats;
    }
}
