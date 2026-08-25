#include "ExperimentParity/ExperimentCacheOptSelfTest.h"

#include "Experiment/Import/VertexCacheOptimization.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace im = experiment::importer;

        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        // 삼각형 하나를 **값으로** 적는다. 인덱스가 바뀌어도 같은 삼각형이면
        // 같은 값이 나와야 한다 — 그것이 이 패스가 지켜야 할 유일한 불변식이다.
        struct TriangleKey final
        {
            std::array<float, 9> positions{};   // 세 정점의 xyz
            std::array<float, 3> uvx{};         // 정점이 실제로 따라왔는지 함께 본다

            [[nodiscard]] bool operator<(const TriangleKey& other) const noexcept
            {
                if (positions != other.positions) return positions < other.positions;
                return uvx < other.uvx;
            }

            // 집합 비교에 필요하다. 근사 비교를 쓰지 않는다 — 재배치는 값을
            // 한 비트도 바꾸면 안 되는 연산이라 정확 비교가 맞는 자다.
            [[nodiscard]] bool operator==(const TriangleKey& other) const noexcept
            {
                return positions == other.positions && uvx == other.uvx;
            }
        };

        [[nodiscard]] std::vector<TriangleKey> TriangleSet(const im::ImportedMesh& mesh)
        {
            std::vector<TriangleKey> keys;
            const auto& s = mesh.streams;
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
            {
                TriangleKey key;
                for (int corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t v = mesh.indices[t + corner];
                    if (v >= s.positions.size()) return {};   // 범위 밖 = 깨진 결과
                    key.positions[corner * 3 + 0] = s.positions[v].x;
                    key.positions[corner * 3 + 1] = s.positions[v].y;
                    key.positions[corner * 3 + 2] = s.positions[v].z;
                    key.uvx[corner] = s.uv0.empty() ? 0.0f : s.uv0[v].x;
                }
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());
            return keys;
        }

        // N×N 격자. 삼각형을 일부러 **뒤섞어** 적어서 캐시에 나쁘게 만든다.
        // 격자는 정점 재사용이 많아 캐시 효과가 뚜렷하게 나온다.
        [[nodiscard]] im::ImportedMesh MakeScrambledGrid(int n)
        {
            im::ImportedMesh mesh;
            mesh.name = "grid";
            for (int y = 0; y <= n; ++y)
            {
                for (int x = 0; x <= n; ++x)
                {
                    mesh.streams.positions.push_back(
                        { static_cast<float>(x), static_cast<float>(y), 0.0f });
                    mesh.streams.uv0.push_back(
                        { static_cast<float>(x) / n, static_cast<float>(y) / n });
                }
            }
            const auto at = [n](int x, int y)
            {
                return static_cast<std::uint32_t>(y * (n + 1) + x);
            };

            // 사각형을 순서대로 적으면 이미 캐시에 좋다. 큰 소수 stride 로
            // 건너뛰며 적어 지역성을 깬다(결정적이라 재현된다).
            const int quads = n * n;
            const int stride = 97;
            for (int step = 0; step < quads; ++step)
            {
                const int q = (step * stride) % quads;
                const int x = q % n;
                const int y = q / n;
                mesh.indices.push_back(at(x, y));
                mesh.indices.push_back(at(x + 1, y));
                mesh.indices.push_back(at(x + 1, y + 1));
                mesh.indices.push_back(at(x, y));
                mesh.indices.push_back(at(x + 1, y + 1));
                mesh.indices.push_back(at(x, y + 1));
            }
            return mesh;
        }

        [[nodiscard]] im::ImportedScene MakeScene(im::ImportedMesh mesh)
        {
            im::ImportedScene scene;
            scene.meshes.push_back(std::move(mesh));
            return scene;
        }
    }

    bool RunExperimentCacheOptSelfTest(std::string& outLog)
    {
        outLog += "[experiment.cacheopt] 합성 검사\n";
        Checker check{ outLog };

        im::ImportOptions options;
        options.optimizeVertexCache = true;

        // ── 1. 캐시에 나쁜 순서가 좋아진다 (양성 대조) ──────────────────
        // ★ 이 한 건이 없으면 실자산의 "개선 0"이 패스 미실행과 구분되지 않는다.
        {
            const im::ImportedMesh before = MakeScrambledGrid(24);
            const std::vector<TriangleKey> expected = TriangleSet(before);

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(before);
            const im::VertexCacheStats stats =
                im::OptimizeVertexCache(scene, options, notes);

            char measured[200];
            std::snprintf(measured, sizeof(measured),
                "  격자 24x24: ACMR %.3f -> %.3f · overfetch %.3f -> %.3f\n",
                stats.acmrBefore, stats.acmrAfter,
                stats.overfetchBefore, stats.overfetchAfter);
            outLog += measured;

            check.Check(1 == stats.meshesProcessed, "격자: 메시 1개 처리");
            check.Check(stats.acmrAfter < stats.acmrBefore, "격자: ACMR 이 낮아졌다");

            // ★ 처음엔 여기서도 "overfetch 가 낮아진다"를 요구했고 실패했다.
            //   실패한 쪽은 구현이 아니라 **내 단정**이었다.
            //
            //   analyzeVertexFetch 는 "가져온 바이트 / 정점 버퍼 크기"다.
            //   모든 정점이 참조되면 순서와 무관하게 전 영역을 한 번씩 가져오므로
            //   1.0 근처가 **이미 최적**이고 순서로 더 낮출 수 있는 값이 아니다
            //   (실측 1.004 -> 1.004). overfetch 가 1 을 넘으려면 참조되지 않는
            //   정점이 섞여 있어야 하고, 그 경우는 아래 "고아 정점" 항목이 본다.
            //
            //   그래서 여기서는 **나빠지지 않았는가**만 묻는다.
            check.Check(stats.overfetchAfter <= stats.overfetchBefore + 1e-6,
                "격자: overfetch 가 나빠지지 않았다");

            // ★ 기하 불변식 — 순서만 바뀌고 삼각형 집합은 같아야 한다.
            const std::vector<TriangleKey> actual = TriangleSet(scene.meshes[0]);
            check.Check(!actual.empty(), "격자: 결과 인덱스가 범위 안이다");
            check.Check(actual == expected, "격자: 삼각형 집합이 값으로 동일하다");
            check.Check(scene.meshes[0].indices.size() == before.indices.size(),
                "격자: 인덱스 수 보존");
        }

        // ── 2. 참조되지 않는 정점은 사라진다 ────────────────────────────
        {
            im::ImportedMesh mesh;
            mesh.name = "orphan";
            for (int i = 0; i < 4; ++i)
            {
                mesh.streams.positions.push_back(
                    { static_cast<float>(i), 0.0f, 0.0f });
                mesh.streams.uv0.push_back({ static_cast<float>(i), 0.0f });
            }
            mesh.indices = { 0, 1, 2 };   // 정점 3 은 아무도 안 쓴다

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            const im::VertexCacheStats stats =
                im::OptimizeVertexCache(scene, options, notes);
            check.Check(4 == stats.verticesBefore, "고아 정점: 전 4개");
            check.Check(3 == stats.verticesAfter, "고아 정점: 후 3개");
            check.Check(3 == scene.meshes[0].streams.VertexCount(),
                "고아 정점: 스트림이 3정점");
            check.Check(3 == scene.meshes[0].streams.uv0.size(),
                "고아 정점: uv0 도 함께 줄었다");
        }

        // ── 3. skin 이 재배치를 따라온다 ────────────────────────────────
        // 재배치에서 skin 을 빠뜨리면 뼈가 엉뚱한 정점에 붙는다.
        {
            im::ImportedMesh mesh = MakeScrambledGrid(6);
            const std::size_t vertexCount = mesh.streams.positions.size();
            mesh.streams.influenceOffsets.push_back(0);
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                // 정점마다 다른 joint 를 준다 — 섞이면 바로 드러난다.
                mesh.streams.influences.push_back(
                    { im::JointIndex(static_cast<std::uint32_t>(v)), 1.0f });
                mesh.streams.influenceOffsets.push_back(
                    static_cast<std::uint32_t>(mesh.streams.influences.size()));
            }
            // 원본에서 "위치 -> joint" 대응을 기억해 둔다.
            std::vector<std::pair<float, std::uint32_t>> expected;
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                expected.emplace_back(mesh.streams.positions[v].x
                    + mesh.streams.positions[v].y * 1000.0f,
                    mesh.streams.InfluencesOf(v)[0].joint.Value());
            }
            std::sort(expected.begin(), expected.end());

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            im::OptimizeVertexCache(scene, options, notes);

            const im::VertexStreams& s = scene.meshes[0].streams;
            std::vector<std::pair<float, std::uint32_t>> actual;
            for (std::size_t v = 0; v < s.VertexCount(); ++v)
            {
                const std::span<const im::JointInfluence> influences = s.InfluencesOf(v);
                actual.emplace_back(s.positions[v].x + s.positions[v].y * 1000.0f,
                    influences.empty() ? ~0u : influences[0].joint.Value());
            }
            std::sort(actual.begin(), actual.end());
            check.Check(actual == expected, "skin 이 정점을 따라 재배치된다");
        }

        // ── 4. 끄면 아무것도 하지 않는다 ────────────────────────────────
        {
            im::ImportOptions off;
            off.optimizeVertexCache = false;
            const im::ImportedMesh before = MakeScrambledGrid(6);
            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(before);
            const im::VertexCacheStats stats =
                im::OptimizeVertexCache(scene, off, notes);
            check.Check(0 == stats.meshesProcessed, "off: 처리 0");
            check.Check(scene.meshes[0].indices == before.indices,
                "off: 인덱스가 그대로다");
        }

        // ── 5. 삼각형 리스트가 아니면 건드리지 않는다 ───────────────────
        {
            im::ImportedMesh mesh = MakeScrambledGrid(4);
            mesh.indices.pop_back();          // 3의 배수가 아니게 만든다
            const std::vector<std::uint32_t> before = mesh.indices;

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            const im::VertexCacheStats stats =
                im::OptimizeVertexCache(scene, options, notes);
            check.Check(0 == stats.meshesProcessed, "3의 배수 아님: 처리 0");
            check.Check(1 == stats.meshesSkipped, "3의 배수 아님: 건너뜀으로 센다");
            check.Check(scene.meshes[0].indices == before,
                "3의 배수 아님: 인덱스가 그대로다");
        }

        // ── 6. 결정적인가 ───────────────────────────────────────────────
        {
            im::ImportNoteSink a, b;
            im::ImportedScene sceneA = MakeScene(MakeScrambledGrid(8));
            im::ImportedScene sceneB = MakeScene(MakeScrambledGrid(8));
            im::OptimizeVertexCache(sceneA, options, a);
            im::OptimizeVertexCache(sceneB, options, b);
            check.Check(sceneA.meshes[0].indices == sceneB.meshes[0].indices,
                "같은 입력은 같은 인덱스를 낸다");
        }

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 통과 %zu · 실패 %zu\n",
            check.passed + check.failed, check.passed, check.failed);
        outLog += summary;
        return 0 == check.failed;
    }
}
