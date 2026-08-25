#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 정점 속성 기술표 — 레이아웃의 정본 (PHASE 4 · 트랙 V1)
//
// ★ 이 표가 존재하는 이유
//
//   같은 사실이 코드 11곳에 손으로 반복돼 있었다. 입력 레이아웃 5곳이 오프셋을
//   숫자로 박고(`{ "TEXCOORD", 0, RGB32Float, 0, 24, 0 }`), 셰이더 VSIn 4곳이
//   같은 순서를 또 적고, `static_assert(offsetof(Vertex, uv0) == 24)` 가 그
//   둘을 손으로 묶는다. 속성 하나를 더하려면 그 열한 곳을 전부 찾아야 하고,
//   빠뜨리면 셰이더가 0을 읽거나 PSO 생성이 실패한다.
//
//   여기서는 **오프셋을 사람이 세지 않는다.** 표의 순서가 메모리 순서이고,
//   오프셋은 마스크에서 계산된다.
//
// ★ 이 표는 "GPU 에 올릴 수 있는 레이아웃"을 기술한다
//
//   현재 `experiment::Vertex` 는 그 대상이 아니다 — 스킨이
//   `(bone, weight)` 인터리브라 HLSL 시맨틱으로 기술할 방법이 없다
//   (ModelImportPipelinePlan §1.8). 한 시맨틱이 stride 8B 로 네 번 밟아야 하는데
//   정점 입력 레이아웃에 그런 표현이 없다.
//
//   V1 시점에 "표가 현실을 기술한다"를 증명할 수 있는 대상은 실제로 GPU 에
//   올라가는 legacy 배치이고, 그 대조 단정은 **검사 계층**에 둔다 — 이 헤더가
//   legacy 헤더를 include 하면 계층이 오염된다.
//
//   V2 에서 `experiment::Vertex` 를 이 표에 맞춘 뒤 대조 대상을 옮긴다.
//
// ★ `Cooked/CookedModelFormat.h` 와의 관계 (I7 이 먼저 도착했다)
//
//   그쪽에도 `kVertexLayoutFields` 라는 기술표가 있다. 대상이 다르다:
//
//     이 표          — GPU 입력 레이아웃. 시맨틱·퍼뮤테이션 축·마스크를 든다.
//     cooked 의 표   — `experiment::Vertex` **직렬화** 계약. 스킨을
//                      `"boneinfluence4"` 하나로 묶는다(인터리브라 나눌 수 없다).
//
//   둘 다 필요하지만 **정본은 하나여야 한다.** I7 이 그 경로를 이미 적어 뒀다 —
//   "트랙 V1 이 만드는 전체 속성 기술표가 오면 이 표는 거기서 유도되도록
//   갈아끼우고 이 파일은 함수 하나만 부른다."
//
//   ★ 아직 갈아끼우지 않는다. 지금 `experiment::Vertex` 는 이 표로 기술할 수
//     없으므로(스킨 인터리브) 유도가 성립하지 않는다. **V2 가 `Vertex` 를 이
//     표에 맞추는 순간** cooked 표를 여기서 유도하도록 바꾼다. 그전에 억지로
//     묶으면 두 표가 서로 다른 것을 기술하면서 같은 이름을 갖게 된다.
//
//   해시 함수도 그때 하나로 합친다. 지금 양쪽에 FNV-1a 가 따로 있는 것은
//   중복이지만, 합치는 시점은 대상이 같아진 뒤다.
//
// ★ 의존 없음
//
//   RHI 타입을 쓰지 않는다. 시맨틱은 문자열로, 포맷은 자체 enum 으로 들고,
//   `RHIFormat` 변환은 렌더 경계에서 한 번 한다. 임포트 계층이 렌더 백엔드를
//   알게 되면 이 표를 임포터가 쓸 수 없다.
namespace experiment
{
    /// 정점 속성. **열거 순서가 곧 메모리 순서다** — 표와 순서가 같아야 하고,
    /// 아래 `static_assert` 가 그것을 지킨다.
    enum class VertexAttribute : std::uint8_t
    {
        Position,
        Normal,
        Uv0,
        Uv1,
        Tangent,
        Bitangent,
        BoneIndices,
        BoneWeights,

        Count,
    };

    /// 속성 하나의 GPU 표현. 크기는 여기서 유도하므로 표가 따로 들지 않는다.
    ///
    /// ★ 이름을 `RHIFormat` 과 맞춘다. 렌더 경계에서 1:1 로 옮겨지므로 대응이
    ///   눈에 보여야 하고, 무엇보다 **`Float3` 같은 이름을 쓰면 안 된다** —
    ///   experiment 가 수학 이주(S4)에서 방금 폐기한 이름이다
    ///   (`Float3` → `math::vector3`). 버린 이름을 다른 의미로 되살리면 호출부가
    ///   어느 규약을 따르는지 흐려진다. 계획 §0 이 별칭을 금지한 것과 같은 이유다.
    ///
    /// ★ `RHIFormat` 자체를 쓰지는 않는다. 임포트 계층이 렌더 백엔드를 알게
    ///   되면 임포터가 이 표를 쓸 수 없다.
    enum class VertexFormat : std::uint8_t
    {
        RG32Float,     // 8B
        RGB32Float,    // 12B
        RGBA32Float,   // 16B
    };

    [[nodiscard]] constexpr std::uint32_t SizeOf(VertexFormat format) noexcept
    {
        switch (format)
        {
        case VertexFormat::RG32Float:   return 8;
        case VertexFormat::RGB32Float:  return 12;
        case VertexFormat::RGBA32Float: return 16;
        }
        return 0;   // 도달 불가 — 새 포맷을 더하면 위에 함께 더한다.
    }

    struct VertexAttributeDesc final
    {
        VertexAttribute attribute{};
        /// 진단·로그용 이름. 해시에는 넣지 않는다 — 이름을 다듬었다고 캐시가
        /// 통째로 무효가 되면 안 된다.
        const char* name{};
        /// HLSL 시맨틱. 셰이더와의 계약이므로 **해시에 들어간다**.
        const char* semantic{};
        std::uint32_t semanticIndex{};
        VertexFormat format{};
        /// 이 속성이 있고 없고로 셰이더가 갈리면 그 퍼뮤테이션 축 이름.
        /// 없으면 nullptr(항상 존재하는 속성).
        ///
        /// 선례: `EnhancedShadowPass` 가 스킨 유무로 `SHADOW_SKINNING` 을 켜고
        /// 입력 레이아웃과 VS 를 함께 가른다. 같은 형태를 표가 소유한다.
        const char* permutationAxis{};
    };

    /// ★ 레이아웃 정본. 순서가 메모리 순서다.
    ///
    /// 현재 항목은 **지금 GPU 가 실제로 읽는 배치**를 그대로 옮긴 것이다
    /// (입력 레이아웃 5곳의 오프셋 0·12·24·40·52·64·80 이 그 증거).
    /// V2 가 이 표를 무손실 64B 로 고친다:
    ///   - `Uv1` 제거(전수에서 uv0 복사본)
    ///   - `Bitangent` 제거 + `Tangent` 를 Float4 로(w = handedness)
    ///   - `BoneIndices` 를 UByte4 로(실측 최댓값 60)
    inline constexpr std::array<VertexAttributeDesc,
        static_cast<std::size_t>(VertexAttribute::Count)> kVertexAttributeTable{{
        { VertexAttribute::Position,    "position",    "POSITION",     0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::Normal,      "normal",      "NORMAL",       0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::Uv0,         "uv0",         "TEXCOORD",     0, VertexFormat::RG32Float, nullptr },
        { VertexAttribute::Uv1,         "uv1",         "TEXCOORD",     1, VertexFormat::RG32Float, "LIGHTMAP" },
        { VertexAttribute::Tangent,     "tangent",     "TANGENT",      0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::Bitangent,   "bitangent",   "BINORMAL",     0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::BoneIndices, "boneIndices", "BLENDINDICES", 0, VertexFormat::RGBA32Float, "SKINNING" },
        { VertexAttribute::BoneWeights, "boneWeights", "BLENDWEIGHT",  0, VertexFormat::RGBA32Float, "SKINNING" },
    }};

    // 표의 인덱스와 enum 값이 어긋나면 오프셋 계산이 통째로 틀린다. 항목을
    // 재배치하거나 enum 순서를 바꿀 때 조용히 깨지는 유일한 자리라 못박는다.
    static_assert([]
    {
        for (std::size_t i = 0; i < kVertexAttributeTable.size(); ++i)
        {
            if (static_cast<std::size_t>(kVertexAttributeTable[i].attribute) != i)
            {
                return false;
            }
        }
        return true;
    }(), "kVertexAttributeTable 의 순서가 VertexAttribute 열거와 어긋난다");

    // ── 마스크 ──────────────────────────────────────────────────────────
    //
    // 메시가 실제로 가진 속성의 집합. 없는 속성은 stride 를 먹지 않는다 —
    // 그래서 정적 메시가 스킨 바이트를 내지 않게 되는 것이 V3 다.

    using VertexAttributeMask = std::uint32_t;

    [[nodiscard]] constexpr VertexAttributeMask Bit(VertexAttribute attribute) noexcept
    {
        return VertexAttributeMask{ 1 } << static_cast<std::uint32_t>(attribute);
    }

    [[nodiscard]] constexpr bool Has(VertexAttributeMask mask,
        VertexAttribute attribute) noexcept
    {
        return (mask & Bit(attribute)) != 0;
    }

    /// 표의 모든 속성. 지금의 96B 레이아웃이 이것이다.
    inline constexpr VertexAttributeMask kAllVertexAttributes = []
    {
        VertexAttributeMask mask = 0;
        for (const VertexAttributeDesc& desc : kVertexAttributeTable)
        {
            mask |= Bit(desc.attribute);
        }
        return mask;
    }();

    /// 어떤 메시에도 반드시 있어야 하는 것. 나머지는 전부 옵셔널이다.
    inline constexpr VertexAttributeMask kRequiredVertexAttributes =
        Bit(VertexAttribute::Position);

    // ── 오프셋·stride ───────────────────────────────────────────────────
    //
    // ★ 사람이 세지 않는다. 표 순서대로 있는 것만 누적한다.

    inline constexpr std::uint32_t kInvalidVertexOffset = 0xFFFFFFFFu;

    [[nodiscard]] constexpr std::uint32_t StrideOf(VertexAttributeMask mask) noexcept
    {
        std::uint32_t stride = 0;
        for (const VertexAttributeDesc& desc : kVertexAttributeTable)
        {
            if (Has(mask, desc.attribute)) stride += SizeOf(desc.format);
        }
        return stride;
    }

    /// 마스크에 없는 속성을 물으면 `kInvalidVertexOffset`. 0 을 돌려주면
    /// position 과 구분되지 않아 호출부가 조용히 틀린다.
    [[nodiscard]] constexpr std::uint32_t OffsetOf(VertexAttributeMask mask,
        VertexAttribute attribute) noexcept
    {
        if (!Has(mask, attribute)) return kInvalidVertexOffset;

        std::uint32_t offset = 0;
        for (const VertexAttributeDesc& desc : kVertexAttributeTable)
        {
            if (desc.attribute == attribute) return offset;
            if (Has(mask, desc.attribute)) offset += SizeOf(desc.format);
        }
        return kInvalidVertexOffset;   // 도달 불가(위에서 Has 를 확인했다)
    }

    [[nodiscard]] constexpr const VertexAttributeDesc& DescOf(
        VertexAttribute attribute) noexcept
    {
        return kVertexAttributeTable[static_cast<std::size_t>(attribute)];
    }

    // ── 레이아웃 버전 ───────────────────────────────────────────────────
    //
    // ★ 손으로 올리는 숫자가 아니다(구 V0). 표에서 **유도**하므로 속성을
    //   더하거나 포맷·시맨틱을 바꾸면 자동으로 달라진다. 버전을 올리는 것을
    //   잊어 캐시가 조용히 오독되는 실패를 구조적으로 없앤다.
    //
    //   I7(cooked 경로)이 이 값을 캐시 헤더에 적고, 불일치면 거부하고
    //   재임포트한다 — legacy `.asset` 이 버전 없이 시작해 겪은 상태를
    //   물려받지 않는다.
    //
    //   해시에 넣는 것: 시맨틱·시맨틱 인덱스·포맷·오프셋. `name` 은 넣지
    //   않는다 — 진단 문자열을 다듬었다고 캐시가 통째로 무효가 되면 안 된다.

    namespace detail
    {
        inline constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
        inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        [[nodiscard]] constexpr std::uint64_t HashByte(std::uint64_t hash,
            std::uint8_t byte) noexcept
        {
            return (hash ^ byte) * kFnvPrime;
        }

        [[nodiscard]] constexpr std::uint64_t HashU32(std::uint64_t hash,
            std::uint32_t value) noexcept
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                hash = HashByte(hash, static_cast<std::uint8_t>(value >> shift));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t HashText(std::uint64_t hash,
            const char* text) noexcept
        {
            if (text == nullptr) return HashByte(hash, 0xFF);
            for (const char* c = text; *c != '\0'; ++c)
            {
                hash = HashByte(hash, static_cast<std::uint8_t>(*c));
            }
            return HashByte(hash, 0);
        }
    }

    /// 이 마스크가 만드는 레이아웃의 정체성.
    [[nodiscard]] constexpr std::uint64_t VertexLayoutHash(
        VertexAttributeMask mask) noexcept
    {
        std::uint64_t hash = detail::kFnvOffsetBasis;
        hash = detail::HashU32(hash, mask);
        for (const VertexAttributeDesc& desc : kVertexAttributeTable)
        {
            if (!Has(mask, desc.attribute)) continue;
            hash = detail::HashText(hash, desc.semantic);
            hash = detail::HashU32(hash, desc.semanticIndex);
            hash = detail::HashU32(hash, static_cast<std::uint32_t>(desc.format));
            hash = detail::HashU32(hash, OffsetOf(mask, desc.attribute));
        }
        return hash;
    }

    /// 표 자체의 정체성(마스크 무관). 표가 바뀌었는지를 나타낸다.
    inline constexpr std::uint64_t kVertexLayoutTableHash =
        VertexLayoutHash(kAllVertexAttributes);

    // 표가 지금 GPU 가 읽는 배치를 기술하는가. 입력 레이아웃 5곳이 손으로 박아
    // 둔 오프셋과 같은 값이 나와야 한다 — 다르면 이 표는 현실이 아니다.
    //
    // ★ V2 가 레이아웃을 바꾸면 이 단정도 함께 바뀐다. 그때 바꾸는 것이 맞고,
    //   지금 통과한다는 사실이 "표가 현실을 기술한다"의 증거다.
    static_assert(StrideOf(kAllVertexAttributes) == 96,
        "표가 만드는 stride 가 현재 정점 크기와 다르다");
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Position) == 0);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Normal) == 12);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Uv0) == 24);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Uv1) == 32);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Tangent) == 40);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::Bitangent) == 52);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::BoneIndices) == 64);
    static_assert(OffsetOf(kAllVertexAttributes, VertexAttribute::BoneWeights) == 80);

    // 없는 속성을 물으면 0 이 아니라 센티널이어야 한다.
    static_assert(OffsetOf(kRequiredVertexAttributes, VertexAttribute::Normal)
        == kInvalidVertexOffset);

    // 마스크가 stride 를 실제로 줄이는가 — V3(스트림 분리)의 전제다.
    //   코어(position·normal·uv0·tangent·bitangent) = 12+12+8+12+12 = 56
    //   스킨 둘을 빼면 96 - 32 = 64
    static_assert(StrideOf(kAllVertexAttributes
        & ~(Bit(VertexAttribute::BoneIndices) | Bit(VertexAttribute::BoneWeights)))
        == 64, "스킨을 뺀 stride 가 64B 여야 한다");

    // 마스크가 다르면 정체성도 달라야 한다. 같으면 캐시가 다른 레이아웃을
    // 같은 것으로 읽는다.
    static_assert(VertexLayoutHash(kAllVertexAttributes)
        != VertexLayoutHash(kAllVertexAttributes & ~Bit(VertexAttribute::Uv1)),
        "마스크가 달라도 레이아웃 해시가 같다 — 캐시가 오독한다");
}
