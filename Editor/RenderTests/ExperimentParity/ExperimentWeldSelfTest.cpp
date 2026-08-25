#include "ExperimentParity/ExperimentWeldSelfTest.h"

#include "Experiment/Import/VertexWelding.h"

#include <cstdio>
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

        // 정점 하나를 스트림 끝에 밀어 넣는다. 인자를 다 받게 해서 "무엇이
        // 달라야 갈라지는가"를 검사가 한눈에 드러내도록 했다.
        void Push(im::VertexStreams& s, math::vector3 position,
            math::vector3 normal, math::vector2 uv0, math::vector2 uv1,
            math::vector4 tangent)
        {
            s.positions.push_back(position);
            s.normals.push_back(normal);
            s.uv0.push_back(uv0);
            s.uv1.push_back(uv1);
            s.tangents.push_back(tangent);
        }

        const math::vector3 kNormal{ 0.0f, 1.0f, 0.0f };
        const math::vector2 kUv0{ 0.25f, 0.5f };
        const math::vector2 kUv1{ 0.75f, 0.125f };
        const math::vector4 kTangent{ 1.0f, 0.0f, 0.0f, 1.0f };

        [[nodiscard]] im::ImportedScene MakeScene(im::ImportedMesh mesh)
        {
            im::ImportedScene scene;
            scene.meshes.push_back(std::move(mesh));
            return scene;
        }

        // 삼각형 하나를 두 번 적되, 두 번째 삼각형의 정점을 첫 번째와 완전히
        // 같은 값으로 둔다 -> 6정점이 3정점으로 합쳐져야 한다.
        [[nodiscard]] im::ImportedMesh MakeDuplicateMesh()
        {
            im::ImportedMesh mesh;
            mesh.name = "dup";
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                Push(mesh.streams, { 0.0f, 0.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);
                Push(mesh.streams, { 1.0f, 0.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);
                Push(mesh.streams, { 0.0f, 1.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);
            }
            mesh.indices = { 0, 1, 2, 3, 4, 5 };
            return mesh;
        }

        // 위치는 같고 한 속성만 다른 정점 쌍. 합쳐지면 안 된다.
        [[nodiscard]] im::ImportedMesh MakeDistinctMesh(int differing)
        {
            im::ImportedMesh mesh;
            mesh.name = "distinct";
            Push(mesh.streams, { 0.0f, 0.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);

            math::vector3 normal = kNormal;
            math::vector2 uv0 = kUv0;
            math::vector2 uv1 = kUv1;
            math::vector4 tangent = kTangent;
            switch (differing)
            {
            case 0: normal = { 0.0f, 0.0f, 1.0f }; break;
            case 1: uv0 = { 0.9f, 0.5f }; break;
            case 2: uv1 = { 0.9f, 0.125f }; break;   // ★ uv1 은 소비자 0 이라 빠뜨리기 쉽다
            case 3: tangent = { 0.0f, 1.0f, 0.0f, -1.0f }; break;  // ★ mikktspace 이음매
            default: break;
            }
            Push(mesh.streams, { 0.0f, 0.0f, 0.0f }, normal, uv0, uv1, tangent);
            mesh.indices = { 0, 1, 0 };
            return mesh;
        }

        [[nodiscard]] const char* DifferingName(int differing)
        {
            switch (differing)
            {
            case 0: return "normal";
            case 1: return "uv0";
            case 2: return "uv1";
            case 3: return "tangent";
            default: return "?";
            }
        }
    }

    bool RunExperimentWeldSelfTest(std::string& outLog)
    {
        outLog += "[experiment.weld] 합성 검사\n";
        Checker check{ outLog };

        im::ImportOptions options;
        options.weldVertices = true;

        // ── 1. 완전히 같은 정점은 합쳐진다 (양성 대조) ──────────────────
        // ★ 이 한 건이 없으면 실자산의 "0개 합침"이 패스 미실행과 구분되지 않는다.
        {
            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(MakeDuplicateMesh());
            const im::VertexWeldStats stats = im::WeldVertices(scene, options, notes);

            check.Check(6 == stats.verticesBefore, "중복 메시: 용접 전 정점 6개");
            check.Check(3 == stats.verticesAfter, "중복 메시: 용접 후 정점 3개");
            check.Check(3 == stats.VerticesRemoved(), "중복 메시: 3개가 제거됐다");
            check.Check(1 == stats.meshesProcessed, "중복 메시: 메시 1개 처리");

            const im::ImportedMesh& mesh = scene.meshes[0];
            check.Check(3 == mesh.streams.VertexCount(), "중복 메시: 스트림이 3정점");
            check.Check(6 == mesh.indices.size(), "중복 메시: 인덱스 수 보존(삼각형 2개)");

            // ★ 인덱스가 실제로 재사상됐는가. 개수만 보면 remap 을 빼먹어도 통과한다.
            const std::vector<std::uint32_t> expected{ 0, 1, 2, 0, 1, 2 };
            check.Check(mesh.indices == expected, "중복 메시: 인덱스가 앞쪽 정점으로 재사상됐다");

            // 값이 살아남았는가 — 합치면서 스트림을 빠뜨리지 않았는지 본다.
            check.Check(3 == mesh.streams.uv1.size(), "중복 메시: uv1 스트림이 따라왔다");
            check.Check(3 == mesh.streams.tangents.size(), "중복 메시: tangent 스트림이 따라왔다");
        }

        // ── 2. 한 속성만 달라도 합치지 않는다 ───────────────────────────
        for (int differing = 0; differing < 4; ++differing)
        {
            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(MakeDistinctMesh(differing));
            const im::VertexWeldStats stats = im::WeldVertices(scene, options, notes);

            const std::string what = std::string(DifferingName(differing))
                + " 만 다른 정점은 합치지 않는다";
            check.Check(2 == stats.verticesAfter, what);
            check.Check(0 == stats.VerticesRemoved(), what + " (제거 0)");
        }

        // ── 3. -0.0 과 +0.0 은 같은 값이다 ──────────────────────────────
        // 부호만 다른 0 이 정점을 쪼개면 용접이 제 일을 못 한다.
        {
            im::ImportedMesh mesh;
            mesh.name = "signed-zero";
            Push(mesh.streams, { 0.0f, 1.0f, 2.0f }, kNormal, kUv0, kUv1, kTangent);
            Push(mesh.streams, { -0.0f, 1.0f, 2.0f }, kNormal, kUv0, kUv1, kTangent);
            mesh.indices = { 0, 1, 0 };

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            const im::VertexWeldStats stats = im::WeldVertices(scene, options, notes);
            check.Check(1 == stats.verticesAfter, "-0.0 과 +0.0 은 같은 정점이다");
        }

        // ── 4. skin weight 만 달라도 합치지 않는다 ──────────────────────
        // skin 은 ValueStreams() 밖(가변 길이)이라 별도로 키에 넣어야 한다.
        // 빠뜨리면 뼈가 다른 정점이 하나로 접힌다.
        {
            im::ImportedMesh mesh;
            mesh.name = "skin";
            Push(mesh.streams, { 0.0f, 0.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);
            Push(mesh.streams, { 0.0f, 0.0f, 0.0f }, kNormal, kUv0, kUv1, kTangent);
            mesh.streams.influenceOffsets = { 0, 1, 2 };
            mesh.streams.influences = {
                { experiment::importer::JointIndex(0), 1.0f },
                { experiment::importer::JointIndex(1), 1.0f } };   // ★ joint 만 다르다
            mesh.indices = { 0, 1, 0 };

            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            const im::VertexWeldStats stats = im::WeldVertices(scene, options, notes);
            check.Check(2 == stats.verticesAfter, "skin joint 만 달라도 합치지 않는다");
            check.Check(2 == scene.meshes[0].streams.influences.size(),
                "skin influence 가 보존된다");
        }

        // ── 5. 끄면 아무것도 하지 않는다 ────────────────────────────────
        {
            im::ImportOptions off;
            off.weldVertices = false;
            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(MakeDuplicateMesh());
            const im::VertexWeldStats stats = im::WeldVertices(scene, off, notes);
            check.Check(0 == stats.meshesProcessed, "weldVertices=false 면 처리 0");
            check.Check(6 == scene.meshes[0].streams.VertexCount(),
                "weldVertices=false 면 정점이 그대로다");
        }

        // ── 6. 인덱스 없는 메시는 건드리지 않는다 ───────────────────────
        // 정점 순서 자체가 의미이므로 합치면 삼각형 구성이 무너진다.
        {
            im::ImportedMesh mesh = MakeDuplicateMesh();
            mesh.indices.clear();
            im::ImportNoteSink notes;
            im::ImportedScene scene = MakeScene(std::move(mesh));
            const im::VertexWeldStats stats = im::WeldVertices(scene, options, notes);
            check.Check(0 == stats.meshesProcessed, "인덱스 없는 메시는 처리하지 않는다");
            check.Check(1 == stats.meshesSkipped, "인덱스 없는 메시는 건너뛴 것으로 센다");
            check.Check(6 == scene.meshes[0].streams.VertexCount(),
                "인덱스 없는 메시는 정점이 그대로다");
        }

        // ── 7. 결정적인가 ───────────────────────────────────────────────
        // 같은 입력이면 같은 결과여야 캐시가 매번 더러워지지 않는다.
        {
            im::ImportNoteSink a, b;
            im::ImportedScene sceneA = MakeScene(MakeDuplicateMesh());
            im::ImportedScene sceneB = MakeScene(MakeDuplicateMesh());
            im::WeldVertices(sceneA, options, a);
            im::WeldVertices(sceneB, options, b);
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
