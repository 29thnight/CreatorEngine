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
#include "../VertexLayout.h"

#include <cstddef>
#include <cstdint>
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
    //
    // 3 (2026-08-27): V3가 mesh별 vertex mask/stride를 도입했다. Vertices 섹션은
    // 더 이상 고정 Vertex[]가 아니라 packed byte blob이고 CookedMesh가 자기
    // byte range·count·stride·mask를 가진다.
    //
    // 4 (2026-09-02): 레이아웃은 그대로인데 **값의 규약**이 바뀌었다. glTF
    //   임포터가 inverseBindMatrix 를 전치된 채(열 벡터 규약) 게시하던 것을
    //   엔진 행 벡터 규약으로 고쳤다. v3 캐시는 16 float 이 같은 자리에 있어
    //   **바이트만 보면 구분이 안 되고**, 신선도 판정은 mtime 이라 소스가 안
    //   바뀐 캐시를 신선하다고 읽는다 — 2 와 같은 이유로 버전이 갈라야 한다.
    //
    // 5 (2026-09-03): glTF UV를 원문 그대로 보존한다. v4 GLB 캐시는 legacy
    //   Assimp parity를 이유로 v = 1 - v를 저장해 텍스처가 수직으로 어긋난다.
    //   레이아웃이 아니라 값의 규약 변경이므로 구버전 cache를 재임포트한다.
    // 6 (2026-09-06): MASK is distinct from Opaque and doubleSided survives
    // material conversion. Older cached values already lost these semantics.
    // 7: preserve emissiveStrength; old caches discarded non-unit strengths.
    // 8: texture references preserve UV selection and affine transform.
    inline constexpr std::uint32_t kFormatVersion = 8u;

    // V3부터 헤더는 특정 mesh mask가 아니라 전체 기술표의 지문을 기록한다.
    // 각 mesh의 실제 배치는 CookedMesh의 mask에서 같은 표로 유도한다.
    [[nodiscard]] constexpr std::uint64_t VertexLayoutTableHash() noexcept
    {
        return kVertexLayoutTableHash;
    }

    static_assert(StrideOf(kCoreVertexAttributes) == 48);
    static_assert(StrideOf(kV2VertexAttributes) == 68);

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
        Vertices = 5,       // mesh별 packed vertex byte blob
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
        std::uint64_t vertexLayoutTableHash{}; // ★ 표 불일치면 거부하고 재임포트
        std::uint32_t maxVertexStride{};       // mesh 레코드와 교차 검증
        std::uint32_t vertexAttributeMaskUnion{};
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
        std::uint32_t vertexByteBegin{};
        std::uint32_t vertexCount{};
        std::uint32_t vertexStride{};
        std::uint32_t vertexAttributeMask{};
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
