#pragma once

// experiment 모델 쿠킹 포맷 — 파일 레이아웃과 버전 규약.
//
// 왜 신설하는가 (실측, 2026-08-25, Release 반복 20):
//
//   자산              쿠킹 총   스켈레톤        메시    재질   텍스처
//   Gunner_F_Mythic   6.257ms  4.981(79.6%)  0.834  0.330   0.012
//   SU_Mythic         7.107ms  5.779(81.3%)  0.929  0.294   0.011
//   Prim_Suzanne      0.311ms  0.000         0.153  0.090   0.003
//
// legacy `.asset` 의 시간은 스켈레톤에 몰려 있고, 원인은 **키의 표현이 디스크와
// 메모리에서 다르다**는 것이다 — legacy 디스크는 packed float4+double,
// legacy 메모리는 SIMD register+double이었다. 그래서 일괄 복사가 원리적으로
// 불가능하고 키마다
// `ifstream::read` 가 두 번 나간다. Gunner 기준 read() 70,647회 중 68,508회
// (97%)가 키인데 바이트로는 43% 다. 스킨 없는 Suzanne 이 read() 22회로 끝나는
// 것이 같은 얘기의 뒷면이다.
//
// 그래서 이 포맷의 규칙은 하나다:
//
//   ★ 가변 개수를 갖는 것은 전부 **연속 POD 블록 + [begin, count) 범위**로 둔다.
//     읽기는 파일을 한 번 읽고 포인터를 전진시키는 것뿐이며, 요소별 read 도
//     요소별 변환도 없다.
//
// ★ mmap 은 쓰지 않는다. 실측에서 ReadFile 보다 느렸다(Gunner 1.85MB 기준
//   ReadFile 0.235ms 대 MapViewOfFile+전 페이지 터치 0.671ms) — 페이지 폴트가
//   memcpy 보다 비싸다. 따라서 zero-copy 를 노린 문자열 테이블 참조나 런타임
//   타입 변경도 근거가 없다. 문자열 테이블은 두되 **복사해서** 쓴다.
//
// ★ 경로 규약은 여기서 정하지 않는다. 쿠킹 산출물이 어디에 놓이는지는
//   SerializationPlan §3.6.1 이 소유하며, 그 주소는 결국 GUID 여야 하는데
//   채번이 §3.4 에서 바뀔 예정이다. 이 파일은 **바이트만** 안다 —
//   경로는 ModelLoadRequest::cookedPath 로 호출자가 들고 온다.

#include "../ModelData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace experiment::cooked
{
    // 'C','E','M','C' — Creator Engine Model Cooked.
    // ★ legacy `.asset` 은 첫 4바이트가 바로 nodeCount 라 구버전을 판별할 수단이
    //   아예 없었다. 그래서 포맷을 바꾸면 조용히 오독한다. 물려받지 않는다.
    inline constexpr std::uint32_t kMagic = 0x434D4543u;

    // 이 파일 **레이아웃**의 개정 번호. 섹션 구성이나 레코드 필드가 바뀌면 올린다.
    // 손으로 올리는 것이 맞다 — 이건 사람이 포맷을 고쳤다는 사실이기 때문이다.
    // 정점 레이아웃은 별개이고 아래에서 **유도**한다.
    //
    // 2 (2026-08-25): bounds 의 **의미**가 바뀌었다. Mathematics 이주로
    //   experiment::Bounds{minimum, maximum} 가 math::aabb{center, extents} 가
    //   됐다. 크기는 24B 로 같고 필드도 vector3 둘이라 **바이트만 보면 구분이
    //   안 된다** — 구버전 캐시를 그대로 읽으면 min 을 center 로, max 를
    //   extents 로 조용히 오독한다. 버전이 있는 이유가 정확히 이것이다.
    inline constexpr std::uint32_t kFormatVersion = 2u;

    // ── 정점 레이아웃 기술표 ────────────────────────────────────────────
    //
    // ★ 여기서 나오는 해시는 **손으로 올리는 숫자가 아니다.** 필드를 더하거나
    //   순서를 바꾸거나 타입을 바꾸면 offsetof/sizeof 가 달라져 해시가 자동으로
    //   달라지고, 구 캐시가 거부된다.
    //
    // 범위 주의: 이것은 **런타임 experiment::Vertex 한정**의 최소 기술표다.
    // 트랙 V1 이 만드는 전체 속성 기술표(이름·시맨틱·포맷·입력 레이아웃까지)가
    // 오면 이 표는 거기서 유도되도록 갈아끼우고 이 파일은 함수 하나만 부른다.
    // 그때 고칠 곳이 한 곳이 되도록 지금부터 함수 하나로 모아 둔다.
    struct VertexFieldDescriptor final
    {
        std::string_view name{};
        std::string_view format{};
        std::uint32_t offset{};
        std::uint32_t size{};
    };

    inline constexpr std::array<VertexFieldDescriptor, 7> kVertexLayoutFields{ {
        { "position",  "float3",        offsetof(Vertex, position),  sizeof(math::vector3) },
        { "normal",    "float3",        offsetof(Vertex, normal),    sizeof(math::vector3) },
        { "uv0",       "float2",        offsetof(Vertex, uv0),       sizeof(math::vector2) },
        { "uv1",       "float2",        offsetof(Vertex, uv1),       sizeof(math::vector2) },
        { "tangent",   "float3",        offsetof(Vertex, tangent),   sizeof(math::vector3) },
        { "bitangent", "float3",        offsetof(Vertex, bitangent), sizeof(math::vector3) },
        { "skin",      "boneinfluence4", offsetof(Vertex, skin),
          sizeof(std::array<BoneInfluence, MaxBoneInfluences>) },
    } };

    namespace detail
    {
        [[nodiscard]] constexpr std::uint64_t FnvAppend(
            std::uint64_t hash, std::string_view text) noexcept
        {
            for (const char c : text)
            {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t FnvAppend(
            std::uint64_t hash, std::uint64_t value) noexcept
        {
            for (int byte = 0; byte < 8; ++byte)
            {
                hash ^= (value >> (byte * 8)) & 0xFFull;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint32_t SumFieldSizes() noexcept
        {
            std::uint32_t total = 0;
            for (const VertexFieldDescriptor& field : kVertexLayoutFields)
                total += field.size;
            return total;
        }
    }

    // 기술표에서 유도되는 정점 레이아웃 지문.
    [[nodiscard]] constexpr std::uint64_t VertexLayoutHash() noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const VertexFieldDescriptor& field : kVertexLayoutFields)
        {
            hash = detail::FnvAppend(hash, field.name);
            hash = detail::FnvAppend(hash, field.format);
            hash = detail::FnvAppend(hash, static_cast<std::uint64_t>(field.offset));
            hash = detail::FnvAppend(hash, static_cast<std::uint64_t>(field.size));
        }
        hash = detail::FnvAppend(hash, static_cast<std::uint64_t>(sizeof(Vertex)));
        return hash;
    }

    // ★ 기술표가 Vertex 를 **전부** 덮는가.
    //
    //   막으려는 유일한 실패: 필드를 추가하고 표에 넣지 않는 것. 그러면 해시가
    //   그 필드를 모르게 되어 레이아웃이 바뀌었는데도 구 캐시를 받아들인다 —
    //   버전 규약이 있는 채로 조용히 오독하는, 가장 나쁜 상태다.
    //
    //   그래서 `sizeof(Vertex) == 96` 같은 상수 비교로 쓰지 않는다. 그 형태는
    //   필드를 더하면서 96 을 108 로 고치는 것만으로 표에서 빠진 채 통과한다 —
    //   이빨이 없다. **표에 적힌 크기의 합**과 비교해야 표를 갱신하지 않고는
    //   통과할 수 없다.
    static_assert(detail::SumFieldSizes() == sizeof(Vertex),
        "정점 기술표가 Vertex 를 전부 덮지 않는다 — 필드를 더했으면 "
        "kVertexLayoutFields 에도 더할 것(빠지면 구 캐시를 조용히 받아들인다)");

    static_assert(kVertexLayoutFields.back().offset
        + kVertexLayoutFields.back().size == sizeof(Vertex),
        "정점 기술표의 마지막 필드가 Vertex 끝과 맞지 않는다 — 순서가 어긋났다");

    // Vertex 가 그대로 memcpy 가능해야 정점 블록을 일괄로 읽을 수 있다.
    static_assert(std::is_trivially_copyable_v<Vertex>,
        "Vertex 가 trivially copyable 이 아니면 정점 블록 일괄 복사가 성립하지 않는다");

    // ★ 키 세 종은 이 포맷이 존재하는 이유다. legacy 가 여기서 시간의 80% 를
    //   쓴 원인이 표현 불일치였으므로, blittable 이 아니게 되는 순간 이 포맷의
    //   근거가 사라진다. 컴파일 시점에 막는다.
    static_assert(std::is_trivially_copyable_v<TranslationKey>
        && std::is_trivially_copyable_v<RotationKey>
        && std::is_trivially_copyable_v<ScaleKey>,
        "애니메이션 키가 trivially copyable 이 아니다 — 일괄 복사가 성립하지 않는다");

    // ── 파일 레이아웃 ───────────────────────────────────────────────────
    //
    // [Header][SectionTable][Section 0][Section 1]...
    //
    // 섹션은 8바이트 정렬로 놓는다. 정렬을 어기면 POD 블록을 그대로 읽을 때
    // 미정렬 접근이 되고, 그건 아키텍처에 따라 조용히 느려지거나 죽는다.
    inline constexpr std::uint64_t kSectionAlignment = 8u;

    enum class SectionKind : std::uint32_t
    {
        Strings = 0,        // 이름 바이트 blob
        Metadata = 1,       // ModelMetadata (경로·이름은 StringRef)
        Nodes = 2,          // CookedNode[]
        NodeMeshes = 3,     // MeshIndex 값 blob (노드가 [begin,count) 로 참조)
        Meshes = 4,         // CookedMesh[]
        Vertices = 5,       // Vertex[]      ← 일괄
        Indices = 6,        // uint32[]      ← 일괄
        Materials = 7,      // 가변 길이 — 개수가 적어 속도와 무관하다
        Bones = 8,          // CookedBone[]
        Clips = 9,          // CookedClip[]
        Channels = 10,      // CookedChannel[]
        TranslationKeys = 11, // TranslationKey[]  ← 일괄
        RotationKeys = 12,    // RotationKey[]     ← 일괄
        ScaleKeys = 13,       // ScaleKey[]        ← 일괄
        Skeleton = 14,      // CookedSkeletonHeader (루트·행렬·존재 여부)
        Animator = 15,      // CookedAnimator
        Count
    };

    // 문자열 테이블 안의 한 조각. 널 종단을 쓰지 않는다 — 길이를 들고 있으면
    // 읽는 쪽이 스캔하지 않아도 되고, 빈 문자열과 없는 문자열이 구분된다.
    struct StringRef final
    {
        std::uint32_t offset{};
        std::uint32_t length{};
    };

#pragma pack(push, 1)

    struct FileHeader final
    {
        std::uint32_t magic{ kMagic };
        std::uint32_t formatVersion{ kFormatVersion };
        std::uint64_t vertexLayoutHash{};   // ★ 불일치면 거부하고 재임포트
        std::uint32_t vertexStride{};
        std::uint32_t vertexAttributeMask{}; // 트랙 V3 이 오면 의미가 생긴다
        std::uint64_t fileBytes{};           // 잘린 파일 판별
        std::uint32_t sectionCount{};
        std::uint32_t reserved{};
    };

    struct SectionEntry final
    {
        std::uint32_t kind{};
        std::uint32_t elementCount{};
        std::uint64_t offset{};
        std::uint64_t bytes{};
    };

    struct CookedNode final
    {
        StringRef name{};
        std::uint32_t parent{};      // Index<Tag> 원값 — 무효는 UINT32_MAX
        std::uint32_t meshBegin{};
        std::uint32_t meshCount{};
        std::uint32_t pad{};
        math::matrix4x4 localTransform{};
    };

    struct CookedMesh final
    {
        StringRef name{};
        std::uint32_t material{};    // Index<Tag> 원값
        std::uint32_t vertexBegin{};
        std::uint32_t vertexCount{};
        std::uint32_t indexBegin{};
        std::uint32_t indexCount{};
        math::aabb bounds{};
    };

    struct CookedBone final
    {
        StringRef name{};
        std::uint32_t parent{};      // Index<Tag> 원값
        std::uint32_t pad{};
        math::matrix4x4 inverseBindMatrix{};
    };

    struct CookedChannel final
    {
        std::uint32_t bone{};        // Index<Tag> 원값
        std::uint8_t translationInterpolation{};
        std::uint8_t rotationInterpolation{};
        std::uint8_t scaleInterpolation{};
        std::uint8_t pad{};
        std::uint32_t translationBegin{};
        std::uint32_t translationCount{};
        std::uint32_t rotationBegin{};
        std::uint32_t rotationCount{};
        std::uint32_t scaleBegin{};
        std::uint32_t scaleCount{};
    };

    struct CookedClip final
    {
        StringRef name{};
        double durationTicks{};
        double ticksPerSecond{};
        std::uint32_t channelBegin{};
        std::uint32_t channelCount{};
        std::uint8_t looping{};
        std::uint8_t pad[7]{};
    };

    struct CookedSkeletonHeader final
    {
        std::uint8_t present{};
        std::uint8_t pad[3]{};
        std::uint32_t rootBone{};    // Index<Tag> 원값
        math::matrix4x4 rootTransform{};
        math::matrix4x4 globalInverseTransform{};
    };

    struct CookedAnimator final
    {
        std::uint8_t present{};
        std::uint8_t pad[3]{};
        std::uint32_t defaultClip{}; // Index<Tag> 원값
        Uuid::Uuid16 motionAssetId{};
    };

    struct CookedMetadata final
    {
        Uuid::Uuid16 assetId{};
        StringRef name{};
        StringRef sourcePath{};
        StringRef cookedPath{};
        std::int64_t sourceWriteTimeTicks{};
        std::uint8_t payloadKind{};
        std::uint8_t pad[7]{};
    };

#pragma pack(pop)
}
