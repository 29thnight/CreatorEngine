#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// PHASE 3.75 MBC6 — model generation 정점 속성 기술표의 제품 정본.
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
//   V2에서 `experiment::Vertex`를 tangent4 + 분리 bone index/weight 배치로
//   바꿨다. 이제 이 표와 런타임 구조를 아래 offsetof/sizeof 단정으로 직접
//   대조한다. legacy `::Vertex`는 I5 전까지 별도 96B 배치로 남는다.
//
// ★ `Cooked/CookedModelFormat.h` 와의 관계 (I7 이 먼저 도착했다)
//
//   V2에서 cooked의 별도 필드 표와 FNV 구현을 제거했다. V3 cooked 헤더는 전체
//   표의 hash와 mask union/max stride를 기록하고, 각 mesh는 이 표에서 유도한
//   자기 mask/stride를 기록·검사한다.
//
// ★ 의존 없음
//
//   RHI 타입을 쓰지 않는다. 시맨틱은 문자열로, 포맷은 자체 enum 으로 들고,
//   `RHIFormat` 변환은 렌더 경계에서 한 번 한다. 임포트 계층이 렌더 백엔드를
//   알게 되면 이 표를 임포터가 쓸 수 없다.
namespace assets
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
        Color,
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
        RGBA8Uint,     // 4B
    };

    [[nodiscard]] constexpr std::uint32_t SizeOf(VertexFormat format) noexcept
    {
        switch (format)
        {
        case VertexFormat::RG32Float:   return 8;
        case VertexFormat::RGB32Float:  return 12;
        case VertexFormat::RGBA32Float: return 16;
        case VertexFormat::RGBA8Uint:   return 4;
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
    /// V2 런타임 배치의 정본. Uv1은 V3/Lightmap이 선택적으로 다시 붙일 수 있게
    /// 표에는 남기되 `kV2VertexAttributes`에서 제외한다.
    inline constexpr std::array<VertexAttributeDesc,
        static_cast<std::size_t>(VertexAttribute::Count)> kVertexAttributeTable{{
        { VertexAttribute::Position,    "position",    "POSITION",     0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::Normal,      "normal",      "NORMAL",       0, VertexFormat::RGB32Float, nullptr },
        { VertexAttribute::Uv0,         "uv0",         "TEXCOORD",     0, VertexFormat::RG32Float, nullptr },
        { VertexAttribute::Uv1,         "uv1",         "TEXCOORD",     1, VertexFormat::RG32Float, "MODEL_VERTEX_UV1" },
        { VertexAttribute::Tangent,     "tangent",     "TANGENT",      0, VertexFormat::RGBA32Float, nullptr },
        { VertexAttribute::Color,       "color",       "COLOR",        0, VertexFormat::RGBA32Float, "MODEL_VERTEX_COLOR" },
        { VertexAttribute::BoneIndices, "boneIndices", "BLENDINDICES", 0, VertexFormat::RGBA8Uint, "MODEL_VERTEX_SKINNING" },
        { VertexAttribute::BoneWeights, "boneWeights", "BLENDWEIGHT",  0, VertexFormat::RGBA32Float, "MODEL_VERTEX_SKINNING" },
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

    /// 표가 기술할 수 있는 모든 속성. V2 고정 구조에는 Uv1이 없고 V3에서
    /// 메시별 마스크가 이 전체 집합의 부분집합을 고른다.
    inline constexpr VertexAttributeMask kAllVertexAttributes = []
    {
        VertexAttributeMask mask = 0;
        for (const VertexAttributeDesc& desc : kVertexAttributeTable)
        {
            mask |= Bit(desc.attribute);
        }
        return mask;
    }();

    /// V3의 모든 메시가 갖는 interleaved core. uv1/color/skin은 메시별 옵셔널이다.
    inline constexpr VertexAttributeMask kCoreVertexAttributes =
        Bit(VertexAttribute::Position)
        | Bit(VertexAttribute::Normal)
        | Bit(VertexAttribute::Uv0)
        | Bit(VertexAttribute::Tangent);

    inline constexpr VertexAttributeMask kSkinVertexAttributes =
        Bit(VertexAttribute::BoneIndices)
        | Bit(VertexAttribute::BoneWeights);

    inline constexpr VertexAttributeMask kRequiredVertexAttributes =
        kCoreVertexAttributes;

    inline constexpr VertexAttributeMask kV2VertexAttributes =
        kCoreVertexAttributes | kSkinVertexAttributes;

    /// MBC6 제품 경로가 반드시 분리하는 네 가지 기본 조합.
    inline constexpr VertexAttributeMask kColorVertexAttributes =
        Bit(VertexAttribute::Color);
    inline constexpr std::array<VertexAttributeMask, 4> kModelVertexMasks{{
        kCoreVertexAttributes,
        kCoreVertexAttributes | kColorVertexAttributes,
        kCoreVertexAttributes | kSkinVertexAttributes,
        kCoreVertexAttributes | kColorVertexAttributes | kSkinVertexAttributes,
    }};

    [[nodiscard]] constexpr bool IsSupportedModelVertexLayout(
        VertexAttributeMask attributes) noexcept
    {
        if ((attributes & ~kAllVertexAttributes) != 0) return false;
        if ((attributes & kCoreVertexAttributes) != kCoreVertexAttributes)
            return false;
        const VertexAttributeMask skin = attributes & kSkinVertexAttributes;
        return skin == 0 || skin == kSkinVertexAttributes;
    }

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

    // V2 고정 레이아웃과 V3 조합의 산술을 표 자체에서 증명한다.
    static_assert(StrideOf(kV2VertexAttributes) == 68);
    static_assert(OffsetOf(kV2VertexAttributes, VertexAttribute::Uv1)
        == kInvalidVertexOffset);
    static_assert(OffsetOf(kV2VertexAttributes, VertexAttribute::Color)
        == kInvalidVertexOffset);

    // 없는 속성을 물으면 0 이 아니라 센티널이어야 한다.
    static_assert(OffsetOf(kRequiredVertexAttributes, VertexAttribute::Uv1)
        == kInvalidVertexOffset);

    static_assert(StrideOf(kCoreVertexAttributes) == 48,
        "V3 core stride 가 48B 여야 한다");
    static_assert(StrideOf(kSkinVertexAttributes) == 20,
        "V3 skin stride 가 20B 여야 한다");

    inline constexpr VertexAttributeMask kCoreColorSkinVertexAttributes =
        kCoreVertexAttributes | kColorVertexAttributes | kSkinVertexAttributes;
    static_assert(StrideOf(kCoreColorSkinVertexAttributes) == 84,
        "core|color|skin stride 가 84B 여야 한다");
    static_assert(OffsetOf(kCoreColorSkinVertexAttributes,
        VertexAttribute::BoneIndices) == 64,
        "core|color|skin bone indices offset은 표에서 64B로 유도돼야 한다");
    static_assert(OffsetOf(kCoreColorSkinVertexAttributes,
        VertexAttribute::BoneWeights) == 68,
        "core|color|skin bone weights offset은 표에서 68B로 유도돼야 한다");

    // 마스크가 다르면 정체성도 달라야 한다. 같으면 캐시가 다른 레이아웃을
    // 같은 것으로 읽는다.
    static_assert(VertexLayoutHash(kAllVertexAttributes)
        != VertexLayoutHash(kAllVertexAttributes & ~Bit(VertexAttribute::Uv1)),
        "마스크가 달라도 레이아웃 해시가 같다 — 캐시가 오독한다");
}
