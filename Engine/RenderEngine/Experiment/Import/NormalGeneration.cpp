#include "NormalGeneration.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        // 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
        // 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다. Normal 접두사.
        [[nodiscard]] Float3 NormalCross(const Float3& a, const Float3& b) noexcept
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x };
        }

        [[nodiscard]] Float3 NormalSub(const Float3& a, const Float3& b) noexcept
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        // 원본 정점 하나를 새 스트림 끝에 복사하고 법선만 갈아 끼운다.
        // 비어 있는 스트림은 비운 채로 둔다 — "속성 없음"을 센티널이 아니라
        // 빈 스트림으로 표현하는 규약이다.
        //
        // ★ 스트림을 손으로 나열하지 않는다(V1). 나열하면 새 스트림이 생겼을 때
        //   이 파일을 아는 사람만 따라올 수 있고, 빠뜨리면 조용히 소실된다.
        //   목록의 정본은 VertexStreams::ValueStreams() 하나다.
        void AppendFlatVertex(const VertexStreams& source, std::uint32_t vertex,
            const Float3& normal, VertexStreams& out)
        {
            // normals — 이 패스가 직접 채운다(아래 push_back).
            // tangents — 의도적으로 버린다. glTF 규약이 "법선이 없으면 제공된
            //   탄젠트는 무시한다"고 정하고, 실제로도 법선 없이 만든 탄젠트는
            //   신뢰할 수 없다. 뒤따르는 탄젠트 생성 패스가 다시 만든다.
            AppendValueStreams(source, vertex, out,
                &VertexStreams::normals, &VertexStreams::tangents);
            out.normals.push_back(normal);
            AppendSkin(source, vertex, out);
        }
    }

    bool GenerateFlatNormals(ImportedMesh& mesh, const std::string& context,
        ImportNoteSink& notes, NormalGenerationStats& stats)
    {
        VertexStreams& streams = mesh.streams;
        const std::size_t vertexCount = streams.VertexCount();

        // 이미 있으면 손대지 않는다 — source 가 정본이다.
        if (streams.normals.size() == vertexCount && vertexCount > 0) return false;

        if (vertexCount == 0 || mesh.indices.empty()) return false;
        if (mesh.indices.size() % 3 != 0)
        {
            notes.Warn(ImportNoteCode::InvalidVertexStreams, context,
                "인덱스가 삼각형 배수가 아니라 법선 생성을 건너뛴다.");
            return false;
        }
        if (!streams.normals.empty())
        {
            // 길이가 어긋난 법선 스트림은 신뢰할 수 없다. 조용히 덮어쓰지 않고
            // 계수한 뒤 다시 만든다.
            notes.Warn(ImportNoteCode::InvalidVertexStreams, context,
                "법선 스트림 길이가 정점 수와 다르다("
                + std::to_string(streams.normals.size()) + " vs "
                + std::to_string(vertexCount) + ") — 버리고 다시 생성한다.");
        }

        VertexStreams flat;
        flat.positions.reserve(mesh.indices.size());
        flat.normals.reserve(mesh.indices.size());
        if (streams.HasSkin())
        {
            flat.influenceOffsets.push_back(0);
            flat.influences.reserve(streams.influences.size());
        }

        std::vector<std::uint32_t> newIndices;
        newIndices.reserve(mesh.indices.size());

        for (std::size_t face = 0; face + 2 < mesh.indices.size(); face += 3)
        {
            const std::uint32_t i0 = mesh.indices[face];
            const std::uint32_t i1 = mesh.indices[face + 1];
            const std::uint32_t i2 = mesh.indices[face + 2];

            const Float3& p0 = streams.positions[i0];
            const Float3& p1 = streams.positions[i1];
            const Float3& p2 = streams.positions[i2];

            Float3 normal = NormalCross(NormalSub(p1, p0), NormalSub(p2, p0));
            const float length = std::sqrt(
                normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (length > 1e-12f)
            {
                normal = { normal.x / length, normal.y / length, normal.z / length };
            }
            else
            {
                // 넓이 0 삼각형은 평면을 정의하지 못한다. 방향을 지어내면
                // 조명이 그 자리에서 튀므로 영벡터로 두고 계수한다 — 게시
                // 검증이 잡거나, 하류가 그 정점을 버릴 수 있게.
                normal = {};
                ++stats.degenerateFaces;
            }

            for (const std::uint32_t vertex : { i0, i1, i2 })
            {
                newIndices.push_back(
                    static_cast<std::uint32_t>(flat.positions.size()));
                AppendFlatVertex(streams, vertex, normal, flat);
            }
        }

        streams = std::move(flat);
        mesh.indices = std::move(newIndices);
        return true;
    }

    NormalGenerationStats GenerateMissingNormals(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes)
    {
        NormalGenerationStats stats;
        if (!options.generateMissingNormals) return stats;

        for (std::size_t i = 0; i < scene.meshes.size(); ++i)
        {
            ImportedMesh& mesh = scene.meshes[i];
            const std::string context = "meshes[" + std::to_string(i) + "]";
            const std::size_t before = mesh.streams.VertexCount();
            stats.verticesBefore += before;

            if (GenerateFlatNormals(mesh, context, notes, stats))
            {
                ++stats.meshesProcessed;
                stats.verticesAfter += mesh.streams.VertexCount();
                notes.Info(ImportNoteCode::MissingVertexAttribute, context,
                    "법선이 없어 평면 법선을 생성했다(glTF 규약·legacy GenNormals"
                    " 와 같은 선택). 면마다 값이 달라야 해 정점이 "
                    + std::to_string(before) + " → "
                    + std::to_string(mesh.streams.VertexCount()) + " 로 늘었다.");
            }
            else
            {
                ++stats.meshesSkipped;
                stats.verticesAfter += before;
            }
        }

        if (stats.degenerateFaces > 0)
        {
            notes.Warn(ImportNoteCode::InvalidVertexStreams, "meshes",
                "넓이 0 삼각형 " + std::to_string(stats.degenerateFaces)
                + "개는 평면을 정의하지 못해 법선을 영벡터로 두었다.");
        }
        return stats;
    }
}
