#include "TangentGeneration.h"

#include "mikktspace.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        // 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
        // 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
        struct TangentWorkspace final
        {
            const ImportedMesh* mesh{};
            // 코너 c(= 면*3 + 정점) 의 결과. mikktspace 가 여기 채운다.
            std::vector<Float4> cornerTangents{};

            [[nodiscard]] std::size_t FaceCount() const noexcept
            {
                return mesh->indices.size() / 3;
            }

            [[nodiscard]] std::uint32_t VertexOf(int face, int vert) const noexcept
            {
                return mesh->indices[static_cast<std::size_t>(face) * 3
                    + static_cast<std::size_t>(vert)];
            }
        };

        [[nodiscard]] TangentWorkspace& Workspace(const SMikkTSpaceContext* context)
        {
            return *static_cast<TangentWorkspace*>(context->m_pUserData);
        }

        int MikkGetNumFaces(const SMikkTSpaceContext* context)
        {
            return static_cast<int>(Workspace(context).FaceCount());
        }

        int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, const int)
        {
            return 3;   // 이 패스는 삼각형만 받는다(호출 전에 검사한다).
        }

        void MikkGetPosition(const SMikkTSpaceContext* context, float out[],
            const int face, const int vert)
        {
            const TangentWorkspace& work = Workspace(context);
            const Float3& p = work.mesh->streams.positions[work.VertexOf(face, vert)];
            out[0] = p.x; out[1] = p.y; out[2] = p.z;
        }

        void MikkGetNormal(const SMikkTSpaceContext* context, float out[],
            const int face, const int vert)
        {
            const TangentWorkspace& work = Workspace(context);
            const Float3& n = work.mesh->streams.normals[work.VertexOf(face, vert)];
            out[0] = n.x; out[1] = n.y; out[2] = n.z;
        }

        void MikkGetTexCoord(const SMikkTSpaceContext* context, float out[],
            const int face, const int vert)
        {
            const TangentWorkspace& work = Workspace(context);
            const Float2& uv = work.mesh->streams.uv0[work.VertexOf(face, vert)];
            out[0] = uv.x; out[1] = uv.y;
        }

        void MikkSetTSpaceBasic(const SMikkTSpaceContext* context,
            const float tangent[], const float sign, const int face, const int vert)
        {
            TangentWorkspace& work = Workspace(context);
            const std::size_t corner = static_cast<std::size_t>(face) * 3
                + static_cast<std::size_t>(vert);
            // w 는 handedness. bitangent = w * cross(normal, tangent) 규약이며
            // SceneToModelDraft 가 같은 규약으로 bitangent 를 푼다.
            work.cornerTangents[corner] = { tangent[0], tangent[1], tangent[2], sign };
        }

        // ── 재용접 ──────────────────────────────────────────────────────
        // 키는 (원본 정점, 탄젠트 4성분의 비트값)이다. mikktspace 가 한 그룹으로
        // 묶은 코너들에는 **같은 값을 써 넣으므로** 정확 비교로 충분하다.
        // epsilon 을 두면 붙이면 안 되는 이음매를 붙일 위험이 생긴다.
        struct WeldKey final
        {
            std::uint32_t vertex{};
            std::uint32_t bits[4]{};

            [[nodiscard]] bool operator==(const WeldKey& other) const noexcept
            {
                return vertex == other.vertex
                    && bits[0] == other.bits[0] && bits[1] == other.bits[1]
                    && bits[2] == other.bits[2] && bits[3] == other.bits[3];
            }
        };

        struct WeldKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const WeldKey& key) const noexcept
            {
                std::size_t hash = key.vertex;
                for (const std::uint32_t bit : key.bits)
                {
                    hash ^= static_cast<std::size_t>(bit) + 0x9e3779b97f4a7c15ULL
                        + (hash << 6) + (hash >> 2);
                }
                return hash;
            }
        };

        [[nodiscard]] std::uint32_t FloatBits(float value) noexcept
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            // -0.0 과 +0.0 은 같은 값으로 취급한다. 부호만 다른 0 이 정점을
            // 쓸데없이 쪼개면 재용접이 제 일을 못 한다.
            if (bits == 0x80000000u) bits = 0u;
            return bits;
        }

        [[nodiscard]] WeldKey MakeWeldKey(std::uint32_t vertex, const Float4& t) noexcept
        {
            WeldKey key;
            key.vertex = vertex;
            key.bits[0] = FloatBits(t.x);
            key.bits[1] = FloatBits(t.y);
            key.bits[2] = FloatBits(t.z);
            key.bits[3] = FloatBits(t.w);
            return key;
        }

        // 원본 정점 하나를 새 스트림 끝에 복사한다. 비어 있는 스트림은 비운 채로
        // 둔다 — "속성 없음"을 센티널이 아니라 빈 스트림으로 표현하는 규약이다.
        void AppendVertex(const VertexStreams& source, std::uint32_t vertex,
            const Float4& tangent, VertexStreams& out)
        {
            out.positions.push_back(source.positions[vertex]);
            if (!source.normals.empty()) out.normals.push_back(source.normals[vertex]);
            if (!source.uv0.empty()) out.uv0.push_back(source.uv0[vertex]);
            if (!source.uv1.empty()) out.uv1.push_back(source.uv1[vertex]);
            if (!source.colors.empty()) out.colors.push_back(source.colors[vertex]);
            out.tangents.push_back(tangent);

            if (source.HasSkin())
            {
                for (const JointInfluence& influence : source.InfluencesOf(vertex))
                {
                    out.influences.push_back(influence);
                }
                out.influenceOffsets.push_back(
                    static_cast<std::uint32_t>(out.influences.size()));
            }
        }
    }

    bool GenerateTangents(ImportedMesh& mesh, const std::string& context,
        ImportNoteSink& notes)
    {
        VertexStreams& streams = mesh.streams;

        if (!streams.tangents.empty()) return false;   // 이미 있다 — 손대지 않는다

        const std::size_t vertexCount = streams.VertexCount();
        if (vertexCount == 0 || mesh.indices.empty())
        {
            return false;
        }
        if (streams.normals.size() != vertexCount)
        {
            notes.Warn(ImportNoteCode::MissingVertexAttribute, context,
                "법선이 없어 탄젠트를 생성할 수 없다 — 법선 생성이 먼저다.");
            return false;
        }
        if (streams.uv0.size() != vertexCount)
        {
            notes.Warn(ImportNoteCode::MissingVertexAttribute, context,
                "UV0 가 없어 탄젠트를 생성할 수 없다 — 탄젠트는 UV 로 정의된다.");
            return false;
        }
        if (mesh.indices.size() % 3 != 0)
        {
            notes.Warn(ImportNoteCode::InvalidVertexStreams, context,
                "인덱스가 삼각형 배수가 아니라 탄젠트 생성을 건너뛴다.");
            return false;
        }

        TangentWorkspace work;
        work.mesh = &mesh;
        work.cornerTangents.assign(mesh.indices.size(), Float4{});

        SMikkTSpaceInterface interface_{};
        interface_.m_getNumFaces = &MikkGetNumFaces;
        interface_.m_getNumVerticesOfFace = &MikkGetNumVerticesOfFace;
        interface_.m_getPosition = &MikkGetPosition;
        interface_.m_getNormal = &MikkGetNormal;
        interface_.m_getTexCoord = &MikkGetTexCoord;
        interface_.m_setTSpaceBasic = &MikkSetTSpaceBasic;

        SMikkTSpaceContext mikkContext{};
        mikkContext.m_pInterface = &interface_;
        mikkContext.m_pUserData = &work;

        if (genTangSpaceDefault(&mikkContext) == 0)
        {
            notes.Warn(ImportNoteCode::MissingVertexAttribute, context,
                "mikktspace 가 탄젠트 생성에 실패했다 — 탄젠트 없이 진행한다.");
            return false;
        }

        // ── 법선 직교화 ──────────────────────────────────────────────────
        // mikktspace 는 퇴화 삼각형의 코너에 **이웃의 탄젠트 공간을 물려준다**
        // (헤더가 명시한 동작). 물려받은 탄젠트는 이 정점의 법선과 직교하지
        // 않아 TBN 이 찌그러진다 — 실측: Gunner 코너 309/31470, 최대 |dot| 0.98.
        // legacy(Assimp CalcTangentSpace)는 재직교화를 하므로 0건이었다.
        //
        // 직교 성분만 남긴다. **이미 직교인 코너에는 항등 연산**이라 정상
        // 데이터에서는 mikktspace 출력이 한 비트도 바뀌지 않고, 퇴화 코너만
        // 바로잡힌다(Suzanne 은 보정 0건으로 실측 확인).
        std::size_t reorthogonalized = 0;
        for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner)
        {
            const Float3& rawNormal = streams.normals[mesh.indices[corner]];
            const float normalLength = std::sqrt(rawNormal.x * rawNormal.x
                + rawNormal.y * rawNormal.y + rawNormal.z * rawNormal.z);
            if (normalLength <= 1e-6f) continue;   // 법선이 없으면 손댈 근거가 없다
            const Float3 n{ rawNormal.x / normalLength,
                rawNormal.y / normalLength, rawNormal.z / normalLength };

            Float4& t = work.cornerTangents[corner];
            const float projection = n.x * t.x + n.y * t.y + n.z * t.z;
            const float before = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            if (before <= 1e-6f) continue;
            if (std::abs(projection) / before <= 1e-4f) continue;   // 이미 직교

            Float3 orthogonal{ t.x - n.x * projection,
                t.y - n.y * projection, t.z - n.z * projection };
            float length = std::sqrt(orthogonal.x * orthogonal.x
                + orthogonal.y * orthogonal.y + orthogonal.z * orthogonal.z);
            if (length <= 1e-6f)
            {
                // 탄젠트가 법선과 완전히 평행이라 투영하면 아무것도 남지 않는다.
                // 방향을 지어낼 근거가 없으므로 법선에 수직인 임의 축을 쓴다.
                const Float3 axis = std::abs(n.x) < 0.9f
                    ? Float3{ 1.0f, 0.0f, 0.0f } : Float3{ 0.0f, 1.0f, 0.0f };
                orthogonal = { axis.y * n.z - axis.z * n.y,
                    axis.z * n.x - axis.x * n.z, axis.x * n.y - axis.y * n.x };
                length = std::sqrt(orthogonal.x * orthogonal.x
                    + orthogonal.y * orthogonal.y + orthogonal.z * orthogonal.z);
                if (length <= 1e-6f) continue;
            }
            t.x = orthogonal.x / length;
            t.y = orthogonal.y / length;
            t.z = orthogonal.z / length;
            ++reorthogonalized;
        }
        if (reorthogonalized > 0)
        {
            notes.Info(ImportNoteCode::MissingVertexAttribute, context,
                "퇴화 삼각형이 이웃 탄젠트를 물려받아 법선과 어긋난 코너 "
                + std::to_string(reorthogonalized) + "/"
                + std::to_string(mesh.indices.size()) + "개를 재직교화했다.");
        }

        // ★ 여기서부터가 규약의 핵심이다. 코너 결과를 기존 인덱스에 그대로
        //   써 넣으면 이음매에서 마지막 면이 이겨 탄젠트가 뭉개진다.
        VertexStreams welded;
        welded.positions.reserve(vertexCount);
        welded.tangents.reserve(vertexCount);
        if (streams.HasSkin())
        {
            welded.influenceOffsets.push_back(0);
            welded.influences.reserve(streams.influences.size());
        }

        std::unordered_map<WeldKey, std::uint32_t, WeldKeyHash> lookup;
        lookup.reserve(vertexCount * 2);

        std::vector<std::uint32_t> newIndices;
        newIndices.reserve(mesh.indices.size());

        for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner)
        {
            const std::uint32_t original = mesh.indices[corner];
            const Float4& tangent = work.cornerTangents[corner];
            const WeldKey key = MakeWeldKey(original, tangent);

            const auto found = lookup.find(key);
            if (found != lookup.end())
            {
                newIndices.push_back(found->second);
                continue;
            }

            const auto fresh = static_cast<std::uint32_t>(welded.positions.size());
            AppendVertex(streams, original, tangent, welded);
            lookup.emplace(key, fresh);
            newIndices.push_back(fresh);
        }

        if (welded.positions.size() > vertexCount)
        {
            notes.Info(ImportNoteCode::MissingVertexAttribute, context,
                "탄젠트 이음매 때문에 정점 "
                + std::to_string(welded.positions.size() - vertexCount)
                + "개가 분리됐다(mikktspace 규약 — 기존 인덱스 재사용 금지).");
        }

        streams = std::move(welded);
        mesh.indices = std::move(newIndices);
        return true;
    }

    TangentGenerationStats GenerateMissingTangents(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes)
    {
        TangentGenerationStats stats;
        if (!options.generateMissingTangents) return stats;

        for (std::size_t i = 0; i < scene.meshes.size(); ++i)
        {
            ImportedMesh& mesh = scene.meshes[i];
            const std::string context = "meshes[" + std::to_string(i) + "]";
            const std::size_t before = mesh.streams.VertexCount();

            if (GenerateTangents(mesh, context, notes))
            {
                ++stats.meshesProcessed;
                stats.verticesBefore += before;
                stats.verticesAfter += mesh.streams.VertexCount();
            }
            else
            {
                ++stats.meshesSkipped;
                stats.verticesBefore += before;
                stats.verticesAfter += before;
            }
        }
        return stats;
    }
}
