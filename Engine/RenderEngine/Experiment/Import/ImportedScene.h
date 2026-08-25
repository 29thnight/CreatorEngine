#pragma once

#include "../ModelData.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// 임포트 전용 중간 표현(IR).
//
// GltfImporter(fastgltf) / FbxImporter(ufbx) 가 이 구조 하나로 수렴하고,
// 후처리(용접 → 탄젠트 생성 → meshoptimizer → LOD)가 이 위에서 돈 뒤,
// 마지막에 ImportedScene → experiment::ModelDraft 변환이 일어난다.
//
// ★ 이 IR 은 ModelDraft 보다 **의도적으로 부유하다**. source 포맷이 들고 있던
//   것을 런타임 모델이 버리더라도 여기서는 일단 보존하고, 무엇이 죽는지는
//   변환 경계 한 곳에서 ImportNote 로 계수한다. 손실이 임포터 구현 안에
//   숨는 것을 구조적으로 막는 것이 이 계층의 존재 이유다.
//
// ★ 좌표계·단위는 임포터가 로드 시점에 엔진 관례로 통일한다(ufbx target_axes/
//   target_unit_meters, glTF Y-up→엔진 축). 따라서 ImportedScene 이후의 어떤
//   코드도 포맷별 관례를 알 필요가 없다. 원본 값은 metadata 에 기록만 한다.
namespace experiment::importer
{
    // 값 타입은 상위 네임스페이스의 것을 그대로 쓴다. 중첩 네임스페이스는
    // **한정 조회 시 바깥을 보지 않으므로**(importer::Float3 는 experiment::Float3
    // 를 찾지 못한다) using 선언으로 끌어와 소비자가 im::Float3 로 쓸 수 있게 한다.
    using experiment::Float2;
    using experiment::Float3;
    using experiment::Float4;
    using experiment::Matrix4;
    using experiment::AssetId;
    using experiment::TextureColorSpace;
    using experiment::TranslationKey;
    using experiment::RotationKey;
    using experiment::ScaleKey;
    using experiment::MaxBoneInfluences;

    struct SceneNodeTag;
    struct ImportMeshTag;
    struct ImportMaterialTag;
    struct ImportTextureTag;
    struct SkinTag;
    struct JointTag;

    using SceneNodeIndex = Index<SceneNodeTag>;
    using ImportMeshIndex = Index<ImportMeshTag>;
    using ImportMaterialIndex = Index<ImportMaterialTag>;
    using ImportTextureIndex = Index<ImportTextureTag>;
    using SkinIndex = Index<SkinTag>;
    // skin 의 joints 배열 안에서의 순번. skeleton bone index 가 아니다 —
    // bone 화는 ModelDraft 변환 경계의 일이다.
    using JointIndex = Index<JointTag>;

    // 행렬이 아니라 T·R·S 분리 값이 정본이다. 애니메이션 채널이 세 축을 따로
    // 키잉하므로 같은 형태여야 하고, 분해 불가능한 행렬(shear)을 조용히
    // 삼키는 대신 변환 시점에 잔차를 계수할 수 있다.
    struct TrsTransform final
    {
        Float3 translation{ 0.0f, 0.0f, 0.0f };
        Float4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };   // 쿼터니언 (x, y, z, w)
        Float3 scale{ 1.0f, 1.0f, 1.0f };            // 비균등 보존
    };

    // 계층 정본은 parent 하나. 임포터가 parent-before-child 로 정렬해 넘긴다
    // (실측: 실자산 bone 배열은 정렬을 보장하지 않으므로 정렬은 임포터 의무).
    struct SceneNode final
    {
        std::string name{};
        SceneNodeIndex parent{};
        TrsTransform local{};
        std::vector<ImportMeshIndex> meshes{};
        // 이 노드가 스킨드 메시를 들고 있을 때의 skin. 계층 자체와는 무관하다.
        SkinIndex skin{};
    };

    struct JointInfluence final
    {
        JointIndex joint{};
        float weight{};
    };

    // SoA. 후처리 패스가 전부 스트림 단위로 동작하고, "속성 없음"을 센티널
    // 값이 아니라 빈 스트림으로 표현할 수 있다. 인터리브(Vertex 구조체화)는
    // ModelDraft 변환에서 딱 한 번 한다.
    //
    // 규약: positions 는 필수이며 그 크기가 정점 수다. 비어 있지 않은 다른
    // 스트림은 모두 같은 크기여야 한다(빈 스트림 = 그 속성 없음).
    struct VertexStreams final
    {
        std::vector<Float3> positions{};
        std::vector<Float3> normals{};
        std::vector<Float2> uv0{};
        std::vector<Float2> uv1{};
        // w = handedness(mikktspace 관례). 비어 있으면 탄젠트 생성 패스 대상.
        std::vector<Float4> tangents{};
        std::vector<Float4> colors{};

        // 가변 길이 influence: 정점 i 의 것은 [offsets[i], offsets[i + 1]).
        // 4개 상한은 런타임 모델의 제약이지 source 의 제약이 아니므로 여기서는
        // 자르지 않는다. 클램프·재정규화는 변환 경계에서 계수와 함께 한다.
        std::vector<std::uint32_t> influenceOffsets{};
        std::vector<JointInfluence> influences{};

        [[nodiscard]] std::size_t VertexCount() const noexcept
        {
            return positions.size();
        }

        [[nodiscard]] bool HasSkin() const noexcept
        {
            return !influences.empty() && !influenceOffsets.empty();
        }

        [[nodiscard]] std::span<const JointInfluence> InfluencesOf(
            std::size_t vertexIndex) const noexcept;

        // ── 값 스트림의 정본 목록 (트랙 V1) ────────────────────────────
        //
        // 정점당 값 하나를 갖는 스트림 전부. 재용접 패스(법선 생성·탄젠트
        // 생성·용접·LOD)가 이 목록을 **순회**해 옮기므로, 새 스트림을 더할 때
        // 손대야 할 곳이 여기 한 곳이다.
        //
        // ★ 예전에는 패스마다 `if (!source.uv1.empty()) out.uv1.push_back(...)`
        //   식으로 **손으로 나열**했다. 한 줄을 빠뜨리면 그 속성이 패스를 지나며
        //   조용히 사라졌고, 경고도 나지 않았다.
        //
        // ★ skin(influences/influenceOffsets)은 정점당 **가변 길이**라 여기 없다.
        //   AppendSkin 이 따로 다룬다.
        [[nodiscard]] static constexpr auto ValueStreams() noexcept
        {
            return std::tuple{
                &VertexStreams::positions,
                &VertexStreams::normals,
                &VertexStreams::uv0,
                &VertexStreams::uv1,
                &VertexStreams::tangents,
                &VertexStreams::colors,
            };
        }
    };

    // ── ValueStreams() 목록이 값 스트림 전부를 덮는가 (트랙 V1) ──────────
    //
    // ★ 이 단정이 막으려는 유일한 실패: **필드를 추가하고 ValueStreams() 에
    //   넣지 않는 것.** 그러면 그 속성이 재용접 패스를 지나며 조용히 사라진다.
    //
    // 그래서 크기를 상수와 비교하지 않고 **목록의 원소 수와 비교한다.** 단순히
    //   `sizeof(VertexStreams) == 8 * sizeof(vector)` 로 쓰면, 필드를 더하면서
    //   8 을 9 로 고치는 것만으로 목록에서 빠진 채 통과한다 — 그 형태는 이빨이
    //   없다.
    //
    // 전제: std::vector<T> 의 크기는 T 와 무관하다(어느 표준 라이브러리든 3포인터).
    static_assert(sizeof(std::vector<Float2>) == sizeof(std::vector<Float3>)
        && sizeof(std::vector<Float4>) == sizeof(std::vector<Float3>)
        && sizeof(std::vector<std::uint32_t>) == sizeof(std::vector<Float3>)
        && sizeof(std::vector<JointInfluence>) == sizeof(std::vector<Float3>),
        "std::vector 의 크기가 원소 타입을 탄다 — 아래 단정의 전제가 깨졌다");

    // 값 스트림(목록에 든 것) + 스킨 2개(influenceOffsets·influences) = 전부.
    static_assert(
        (std::tuple_size_v<decltype(VertexStreams::ValueStreams())> + 2)
            * sizeof(std::vector<Float3>) == sizeof(VertexStreams),
        "VertexStreams 의 필드와 ValueStreams() 목록이 어긋난다 — "
        "값 스트림을 더했으면 목록에도 더할 것(빠지면 재용접에서 조용히 소실된다)");

    namespace detail
    {
        // 멤버 포인터 두 개가 같은가. 타입이 다르면 == 가 성립하지 않으므로
        // 컴파일 시점에 갈라 준다.
        template <class A, class B>
        [[nodiscard]] constexpr bool SameStream(A a, B b) noexcept
        {
            if constexpr (std::is_same_v<A, B>) return a == b;
            else                                return false;
        }
    }

    /// 원본 정점 하나의 **값 스트림 전부**를 out 끝에 복사한다.
    ///
    /// 비어 있는 스트림은 비운 채로 둔다 — "속성 없음"을 센티널이 아니라 빈
    /// 스트림으로 표현하는 규약(이 파일 상단 ★)을 그대로 지킨다.
    ///
    /// `excluded` 에 넘긴 스트림은 건너뛴다. 그 패스가 **직접 채우는** 것이거나
    /// (법선 생성의 normals) 의도적으로 **버리는** 것이다(법선 생성의 tangents).
    /// 어느 쪽이든 호출부에 이유를 적어 둔다.
    template <class... Excluded>
    inline void AppendValueStreams(const VertexStreams& source,
        std::size_t vertex, VertexStreams& out, Excluded... excluded)
    {
        std::apply([&](auto... members)
        {
            const auto copyOne = [&](auto member)
            {
                if constexpr (sizeof...(Excluded) > 0)
                {
                    if ((detail::SameStream(member, excluded) || ...)) return;
                }
                const auto& stream = source.*member;
                if (stream.empty()) return;
                (out.*member).push_back(stream[vertex]);
            };
            (copyOne(members), ...);
        }, VertexStreams::ValueStreams());
    }

    /// 원본 정점 하나의 skin influence 를 out 끝에 복사하고 offset 을 닫는다.
    /// 스킨이 없으면 아무것도 하지 않는다(빈 스트림 규약).
    inline void AppendSkin(const VertexStreams& source, std::size_t vertex,
        VertexStreams& out)
    {
        if (!source.HasSkin()) return;
        for (const JointInfluence& influence : source.InfluencesOf(vertex))
        {
            out.influences.push_back(influence);
        }
        out.influenceOffsets.push_back(
            static_cast<std::uint32_t>(out.influences.size()));
    }

    // glTF primitive 하나 = 메시 하나(재질별로 이미 갈라져 있다).
    // FbxImporter 도 재질별 분할 후 이 형태로 맞춘다.
    struct ImportedMesh final
    {
        std::string name{};
        ImportMaterialIndex material{};
        VertexStreams streams{};
        std::vector<std::uint32_t> indices{};
    };

    enum class AlphaMode : std::uint8_t
    {
        Opaque,
        Mask,
        Blend,
    };

    enum class TextureWrap : std::uint8_t
    {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    struct TextureSlot final
    {
        ImportTextureIndex texture{};
        std::uint32_t uvSet{};
        Float2 offset{ 0.0f, 0.0f };
        Float2 tiling{ 1.0f, 1.0f };
        TextureWrap wrapU{ TextureWrap::Repeat };
        TextureWrap wrapV{ TextureWrap::Repeat };

        [[nodiscard]] bool IsValid() const noexcept { return texture.IsValid(); }
    };

    // 의미 기반 PBR 기술이다. shader property 이름(_BaseColor 등)이나
    // ShaderMeta GUID 는 여기 없다 — 그 매핑은 엔진 정책이므로 ModelDraft
    // 변환 경계에서 결정한다(M-phase 정본과 만나는 지점).
    struct ImportedMaterial final
    {
        std::string name{};

        Float4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallicFactor{ 1.0f };
        float roughnessFactor{ 1.0f };
        Float3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float emissiveStrength{ 1.0f };
        float normalScale{ 1.0f };
        float occlusionStrength{ 1.0f };

        AlphaMode alphaMode{ AlphaMode::Opaque };
        float alphaCutoff{ 0.5f };
        bool doubleSided{};

        TextureSlot baseColor{};
        TextureSlot metallicRoughness{};
        TextureSlot normal{};
        TextureSlot occlusion{};
        TextureSlot emissive{};
    };

    // GLB/FBX 임베디드 텍스처는 바이트를 여기서 소유한다. 디스크로 뽑아내는
    // 시점·위치는 임포트 정책이므로 IR 이 결정하지 않는다.
    struct ImportedTexture final
    {
        std::string name{};
        std::filesystem::path sourcePath{};
        std::vector<std::byte> embeddedBytes{};
        std::string mimeType{};
        TextureColorSpace colorSpace{ TextureColorSpace::Linear };

        [[nodiscard]] bool IsEmbedded() const noexcept
        {
            return !embeddedBytes.empty();
        }
    };

    // ★ "Skeleton" 이 아니다. source 포맷에는 bone 이라는 별도 개념이 없고
    //   skin 이 참조하는 node 목록만 있다. skeleton 파생(bone 배열·위상 정렬·
    //   inverse bind 재배치)은 ModelDraft 변환 경계의 일이다.
    struct ImportedSkin final
    {
        std::string name{};
        SceneNodeIndex skeletonRoot{};
        std::vector<SceneNodeIndex> joints{};
        std::vector<Matrix4> inverseBind{};   // joints 와 같은 길이
    };

    enum class KeyInterpolation : std::uint8_t
    {
        Linear,
        Step,
        CubicSpline,
    };

    struct ImportedChannel final
    {
        // ★ bone 이 아니라 node 를 타깃한다. joint 가 아닌 노드를 타깃하는
        //   채널(실측: Gunner 에서 클립당 1개, 620 중 10개)이 여기서는 살아
        //   있고, 변환 경계에서 베이크할지 버릴지 결정·계수한다.
        SceneNodeIndex target{};
        KeyInterpolation translationInterpolation{ KeyInterpolation::Linear };
        KeyInterpolation rotationInterpolation{ KeyInterpolation::Linear };
        KeyInterpolation scaleInterpolation{ KeyInterpolation::Linear };

        // 시간 단위는 초(ImportedClip 참조). 오름차순 정렬은 임포터 의무.
        std::vector<TranslationKey> translations{};
        std::vector<RotationKey> rotations{};
        std::vector<ScaleKey> scales{};       // Float3 — 비균등 scale 보존
    };

    // 시간 정본은 **초**다. tick/ticksPerSecond 쌍은 FBX 관례이므로 임포터가
    // 여기서 이미 초로 환산한다(원본 tps 는 metadata 에 기록).
    struct ImportedClip final
    {
        std::string name{};
        double durationSeconds{};
        std::vector<ImportedChannel> channels{};
    };

    enum class ImportNoteSeverity : std::uint8_t
    {
        Info,
        Warning,
        Error,
    };

    enum class ImportNoteCode : std::uint16_t
    {
        // 임포터가 남기는 것
        UnsupportedFeature,          // 카메라·라이트·모프타깃 등 현 스코프 밖
        MissingVertexAttribute,      // 법선/탄젠트 없음 → 생성 패스 대상
        EmbeddedTextureExtracted,
        ShearedNodeTransform,        // TRS 분해 잔차
        OriginalAxisConverted,

        // 변환 경계(ImportedScene → ModelDraft)가 남기는 것
        NonJointChannelTarget,       // joint 아닌 node 채널 — 베이크 또는 탈락
        InfluenceBudgetExceeded,     // 5개 이상 → 상위 4개 + 재정규화
        UnsupportedInterpolation,    // CubicSpline → Linear 강등 (Step 은 보존됨)
        KeyTimeCollapsed,            // 초→tick 환산이 키를 같은 tick 에 뭉침
        NonUniformScaleDropped,      // 런타임이 uniform scale 만 쓰는 경우
        MaterialSemanticUnmapped,    // PBR semantic → shader property 미매핑

        // 구조 검증
        InvalidSceneStructure,
        InvalidVertexStreams,
        InvalidSkin,
        InvalidAnimation,
    };

    // 손실·미지원을 계수와 함께 기록한다. 같은 종류가 반복될 때 항목을 정점
    // 수만큼 증식시키지 않고 count 를 올린다(silent cap 금지, 로그 폭증 금지).
    struct ImportNote final
    {
        ImportNoteSeverity severity{ ImportNoteSeverity::Warning };
        ImportNoteCode code{ ImportNoteCode::UnsupportedFeature };
        std::string context{};
        std::string message{};
        std::uint32_t count{ 1 };
    };

    // 같은 (code, context) 의 반복은 항목을 늘리지 않고 count 를 올린다.
    // 손상된 대형 메시가 정점 수만큼 note 를 증식시키는 것을 막으면서도 몇
    // 건이었는지는 잃지 않는다. 임포터와 변환 경계가 함께 쓴다.
    class ImportNoteSink final
    {
    public:
        void Add(ImportNoteSeverity severity, ImportNoteCode code,
            std::string context, std::string message);
        void Info(ImportNoteCode code, std::string context, std::string message);
        void Warn(ImportNoteCode code, std::string context, std::string message);
        void Error(ImportNoteCode code, std::string context, std::string message);

        // 다른 sink 가 모은 노트를 개수까지 보존해 흡수한다.
        // ★ Add 를 반복 호출해 합치면 안 된다 — 원본 count 가 1 로 뭉개진다.
        //   메시 단위 병렬 패스가 스레드마다 sink 를 따로 두고 순서대로
        //   합칠 때 쓴다(결정론 유지).
        void Absorb(std::vector<ImportNote> other);

        [[nodiscard]] bool HasErrors() const noexcept;
        [[nodiscard]] std::span<const ImportNote> View() const noexcept
        {
            return notes_;
        }
        [[nodiscard]] std::vector<ImportNote> Release() noexcept
        {
            return std::move(notes_);
        }

    private:
        std::vector<ImportNote> notes_{};
    };

    struct ImportMetadata final
    {
        std::filesystem::path sourcePath{};
        std::string importerName{};      // "GltfImporter(fastgltf)" 등
        std::string importerVersion{};
        std::string generator{};         // 소스 파일이 밝힌 제작 도구

        // 임포터가 엔진 관례로 통일하기 전의 원본 값. 재현·진단용 기록이며
        // 이 값을 보고 분기하는 하류 코드가 있으면 안 된다.
        std::string originalUpAxis{};
        double originalUnitMeters{ 1.0 };
        double originalTicksPerSecond{};
        double appliedUnitScale{ 1.0 };
    };

    struct ImportedScene final
    {
        ImportMetadata metadata{};
        std::vector<SceneNode> nodes{};
        std::vector<ImportedMesh> meshes{};
        std::vector<ImportedMaterial> materials{};
        std::vector<ImportedTexture> textures{};
        std::vector<ImportedSkin> skins{};
        std::vector<ImportedClip> clips{};
        std::vector<ImportNote> notes{};
    };

    struct ImportOptions final
    {
        bool generateMissingNormals{ true };
        bool generateMissingTangents{ true };
        bool weldVertices{ true };
        bool optimizeVertexCache{ true };
        std::uint32_t lodLevels{ 0 };        // 0 = LOD 생성 안 함
        // meshlet 은 소비자(mesh shader 경로)가 생길 때까지 기본 비활성.
        bool buildMeshlets{ false };
    };

    struct ImportRequest final
    {
        std::filesystem::path sourcePath{};
        ImportOptions options{};
    };

    struct ImportResult final
    {
        std::optional<ImportedScene> scene{};
        std::vector<ImportNote> notes{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return scene.has_value();
        }
    };

    // 포맷별 임포터의 경계. Scene·Entity·DataSystem singleton 에 접근하지 않고
    // 완전 소유 ImportedScene 만 반환한다(experiment::IModelDecoder 와 같은 규약).
    class IAssetImporter
    {
    public:
        virtual ~IAssetImporter() = default;
        [[nodiscard]] virtual bool CanImport(
            const std::filesystem::path& sourcePath) const = 0;
        [[nodiscard]] virtual ImportResult Import(const ImportRequest& request) = 0;
    };

    [[nodiscard]] std::size_t TotalVertexCount(const ImportedScene& scene) noexcept;
    [[nodiscard]] std::size_t TotalTriangleCount(const ImportedScene& scene) noexcept;

    // 임포터가 계약을 지켰는지 검사한다. 임포터 구현의 자가 검증이자,
    // 변환 경계가 전제로 삼는 불변식(정렬·인덱스 범위·스트림 길이 일치·
    // influence offset 단조·키 시간 정렬)의 정본이다.
    [[nodiscard]] std::vector<ImportNote> ValidateImportedScene(
        const ImportedScene& scene);

    [[nodiscard]] std::string_view ToString(ImportNoteCode code) noexcept;
}
