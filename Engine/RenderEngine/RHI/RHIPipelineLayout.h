#pragma once
#include <cstdint>
#include <span>

// 파이프라인 레이아웃 — 백엔드 중립 (PHASE 3-1 재정의, V4).
//
// ── 무엇을 옮기는가 ──
//
// 루트 시그니처다. 경계는 이미 서 있었다(IRenderRootSignatureCache::GetOrCreate,
// PHASE 3-4) — 다만 그 경계를 지나가는 **설명**이 D3D12_ROOT_SIGNATURE_DESC
// 그대로였다. 패스 25곳이 D3D12_ROOT_PARAMETER 와 D3D12_DESCRIPTOR_RANGE 를
// 손으로 채워서 넘긴다. 인터페이스만 중립이고 인자가 DX12 면 Vulkan 백엔드는
// 그 인자를 읽을 수 없으므로, 경계가 있으나 없으나 같다.
//
// 샘플러 설명도 여기 있다. R3-2 가 RHISamplerTable(거는 동작)만 중립화하고
// "만드는 쪽의 중립화는 필터·주소 모드·비교 함수를 전부 옮기는 별개의 몫"
// 이라고 적어 두었는데, 그 몫이 이 슬라이스다. 정적 샘플러(루트 시그니처
// 안)와 힙 샘플러(디스크립터 힙)가 **같은 상태**를 서로 다른 DX12 구조체로
// 적고 있어서, 한쪽만 옮기면 어휘가 두 벌이 된다.
//
// ── 열거를 실사용에서 뽑았다 (V1 과 같은 규칙) ──
//
// 리포 전체에서 실제로 쓰이는 값은 놀랄 만큼 적다(2026-08-10 실측):
// 필터 3종 · 주소 모드 3종 · 가시성 3종 · 비교 함수 1종 · 경계색 1종 ·
// 루트 플래그 1종. 25곳이 쓰는 어휘가 이만큼이라는 뜻이다. "언젠가 쓸지
// 모르는" 것을 넣지 않는다 — 백엔드마다 대응표가 늘고, 쓰지 않는 항목은
// 틀려도 아무도 모른다.

// 어느 셰이더 단계가 이 자리를 보는가.
//
// ★ Vulkan 의 VkShaderStageFlags 는 비트 마스크라 조합이 되고 D3D12 의
//   D3D12_SHADER_VISIBILITY 는 단일 값이라 조합이 안 된다. 조합을 허용하는
//   쪽으로 맞추면 DX12 백엔드가 표현할 수 없는 요청을 받게 되므로, 좁은
//   쪽에 맞춘다. 실사용도 셋뿐이다.
enum class RHIShaderVisibility
{
    All,
    Vertex,
    Pixel,
};

// ── 샘플러 ──

enum class RHIFilterMode
{
    Point,
    Linear,
};

enum class RHIAddressMode
{
    Wrap,
    Clamp,
    Border,
};

// None 이 "비교 샘플러가 아니다"를 뜻한다.
//
// ★ D3D12 는 비교 여부를 D3D12_FILTER 값 안에 접어 넣지만(COMPARISON_ 접두)
//   Vulkan 은 compareEnable 이라는 별도 스위치다. 접어 넣은 쪽을 그대로
//   쓰면 Vulkan 백엔드가 그것을 도로 펴야 하고, 필터 종류가 늘 때마다
//   'COMPARISON_ 가 붙은 변종'이 함께 늘어난다. 펴진 형태로 든다.
enum class RHICompareOp
{
    None,
    Less,        ///< V6 이 더했다 — 깊이 테스트의 기본값이다
    LessEqual,
};

enum class RHIBorderColor
{
    TransparentBlack,
    OpaqueWhite,
};

struct RHISamplerDesc
{
    // D3D12_FLOAT32_MAX. 실측한 모든 샘플러가 이 값을 쓴다 — 기본값으로
    // 두면 25곳에서 그 줄이 사라진다. 0(구조체 영초기화의 값)이면 밉 0만
    // 읽히므로, 기본값이 곧 함정인 자리다.
    static constexpr float kMaxLod = 3.402823466e+38f;

    // ★ min/mag 와 mip 을 갈라 든다. D3D12_FILTER 는 셋을 한 값에 접어
    //   넣지만 Vulkan 은 magFilter · minFilter · mipmapMode 가 각각이다.
    //   실제로 쓰이는 COMPARISON_MIN_MAG_LINEAR_MIP_POINT 가 min/mag 와
    //   mip 이 다른 경우라, 하나로 합치면 그 하나를 표현할 수 없다.
    RHIFilterMode  minMag{ RHIFilterMode::Point };
    RHIFilterMode  mip{ RHIFilterMode::Point };

    RHIAddressMode addressU{ RHIAddressMode::Wrap };
    RHIAddressMode addressV{ RHIAddressMode::Wrap };
    RHIAddressMode addressW{ RHIAddressMode::Wrap };

    RHICompareOp   compare{ RHICompareOp::None };
    RHIBorderColor border{ RHIBorderColor::TransparentBlack };
    float          maxLod{ kMaxLod };
};

// 정적 샘플러 = 샘플러 상태 + 그것이 걸리는 자리.
//
// ★ 힙 샘플러와 상태를 공유하되 타입은 가른다. 힙 샘플러에는 레지스터도
//   가시성도 없다 — 한 구조체에 넣고 "그쪽에서는 무시한다"고 적어 두면
//   무시되는 필드를 채우는 호출부가 반드시 생긴다. Vulkan 에서도 이 구분이
//   그대로다: 상태는 VkSampler, 거는 자리는 디스크립터 레이아웃의
//   pImmutableSamplers 다.
struct RHIStaticSamplerDesc
{
    RHISamplerDesc      sampler{};
    uint32_t            shaderRegister{ 0 };
    RHIShaderVisibility visibility{ RHIShaderVisibility::All };
};

namespace RHISampler
{
    constexpr RHISamplerDesc Point(RHIAddressMode address = RHIAddressMode::Clamp)
    {
        RHISamplerDesc desc{};
        desc.minMag = RHIFilterMode::Point;
        desc.mip = RHIFilterMode::Point;
        desc.addressU = desc.addressV = desc.addressW = address;
        return desc;
    }

    constexpr RHISamplerDesc Linear(RHIAddressMode address = RHIAddressMode::Clamp)
    {
        RHISamplerDesc desc{};
        desc.minMag = RHIFilterMode::Linear;
        desc.mip = RHIFilterMode::Linear;
        desc.addressU = desc.addressV = desc.addressW = address;
        return desc;
    }

    // 비교 샘플러. min/mag 는 선형, mip 은 포인트다 — 그림자 맵은 밉이
    // 없고, 비교 결과를 밉 사이에서 섞으면 의미가 없다.
    constexpr RHISamplerDesc Comparison(RHICompareOp op, RHIAddressMode address,
        RHIBorderColor border = RHIBorderColor::TransparentBlack)
    {
        RHISamplerDesc desc{};
        desc.minMag = RHIFilterMode::Linear;
        desc.mip = RHIFilterMode::Point;
        desc.addressU = desc.addressV = desc.addressW = address;
        desc.compare = op;
        desc.border = border;
        return desc;
    }
}

// ── 레이아웃 ──

enum class RHIDescriptorType
{
    ShaderResource,
    UnorderedAccess,
    // Structured/RWStructuredBuffer descriptor table. DX12 encodes this with
    // the same UAV range type as an image, while Vulkan requires the layout
    // to choose VK_DESCRIPTOR_TYPE_STORAGE_BUFFER up front.
    UnorderedAccessBuffer,
    Sampler,
};

// 연속한 레지스터 한 묶음. 실측한 25곳이 전부 테이블당 범위 하나라
// 파라미터가 범위를 직접 든다 — 배열과 개수를 두면 채우는 쪽이 포인터
// 수명을 신경 써야 하는데(아래 ★ 참조) 그 대가를 아무도 쓰지 않는 일에
// 치르게 된다. 필요해지면 그때 배열로 넓힌다.
struct RHIDescriptorRange
{
    RHIDescriptorType type{ RHIDescriptorType::ShaderResource };
    uint32_t          count{ 0 };
    uint32_t          baseRegister{ 0 };
};

enum class RHILayoutParamKind
{
    // 디스크립터 테이블 ↔ Vulkan 의 디스크립터 셋 한 벌.
    DescriptorTable,

    // 루트 디스크립터 — 테이블을 거치지 않고 바로 걸리는 버퍼.
    // Vulkan 에서는 동적 유니폼/스토리지 버퍼가 이 자리다.
    ConstantBuffer,
    ShaderResourceBuffer,
    UnorderedAccessBuffer,

    // 루트 상수 ↔ Vulkan 의 푸시 상수.
    Constants,
};

struct RHIPipelineLayoutParam
{
    RHILayoutParamKind  kind{ RHILayoutParamKind::DescriptorTable };
    RHIShaderVisibility visibility{ RHIShaderVisibility::All };

    RHIDescriptorRange  table{};          // kind == DescriptorTable
    uint32_t            shaderRegister{ 0 };  // 나머지 전부
    uint32_t            constantCount{ 0 };   // kind == Constants (32비트 워드 수)
};

// ★ span 이 가리키는 배열은 호출 동안만 살아 있으면 된다. 캐시가 이것을
//   해시해 루트 시그니처를 만들고 끝내므로, 설명을 붙들지 않는다.
//   D3D12_ROOT_SIGNATURE_DESC 가 포인터를 들어서 생긴 함정(구조체를 통째로
//   바이트 해시하면 주소를 해시하는 꼴)이 여기서도 같으므로, 캐시는
//   반드시 내용으로 해시한다 — DX12RootSignatureCache 의 주석 그대로다.
struct RHIPipelineLayoutDesc
{
    std::span<const RHIPipelineLayoutParam> params;
    std::span<const RHIStaticSamplerDesc>   staticSamplers;

    // 정점 입력 레이아웃을 쓰는가. D3D12 는 루트 시그니처 플래그로 이것을
    // 받고(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), Vulkan 은 파이프라인의
    // 정점 입력 상태로 받는다 — 어느 쪽이든 상위가 아는 사실은 하나뿐이라
    // bool 로 든다. 실측한 루트 플래그도 이것 하나뿐이다.
    bool allowInputAssembler{ false };
};

namespace RHILayout
{
    constexpr RHIPipelineLayoutParam Cbv(uint32_t shaderRegister,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        RHIPipelineLayoutParam param{};
        param.kind = RHILayoutParamKind::ConstantBuffer;
        param.visibility = visibility;
        param.shaderRegister = shaderRegister;
        return param;
    }

    constexpr RHIPipelineLayoutParam Srv(uint32_t shaderRegister,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        RHIPipelineLayoutParam param{};
        param.kind = RHILayoutParamKind::ShaderResourceBuffer;
        param.visibility = visibility;
        param.shaderRegister = shaderRegister;
        return param;
    }

    constexpr RHIPipelineLayoutParam Uav(uint32_t shaderRegister,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        RHIPipelineLayoutParam param{};
        param.kind = RHILayoutParamKind::UnorderedAccessBuffer;
        param.visibility = visibility;
        param.shaderRegister = shaderRegister;
        return param;
    }

    constexpr RHIPipelineLayoutParam Constants(uint32_t shaderRegister, uint32_t wordCount,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        RHIPipelineLayoutParam param{};
        param.kind = RHILayoutParamKind::Constants;
        param.visibility = visibility;
        param.shaderRegister = shaderRegister;
        param.constantCount = wordCount;
        return param;
    }

    constexpr RHIPipelineLayoutParam Table(RHIDescriptorType type, uint32_t count,
        uint32_t baseRegister = 0, RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        RHIPipelineLayoutParam param{};
        param.kind = RHILayoutParamKind::DescriptorTable;
        param.visibility = visibility;
        param.table = { type, count, baseRegister };
        return param;
    }

    constexpr RHIPipelineLayoutParam SrvTable(uint32_t count, uint32_t baseRegister = 0,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        return Table(RHIDescriptorType::ShaderResource, count, baseRegister, visibility);
    }

    constexpr RHIPipelineLayoutParam UavTable(uint32_t count, uint32_t baseRegister = 0,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        return Table(RHIDescriptorType::UnorderedAccess, count, baseRegister, visibility);
    }

    constexpr RHIPipelineLayoutParam UavBufferTable(uint32_t count, uint32_t baseRegister = 0,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        return Table(RHIDescriptorType::UnorderedAccessBuffer,
            count, baseRegister, visibility);
    }

    constexpr RHIPipelineLayoutParam SamplerTable(uint32_t count, uint32_t baseRegister = 0,
        RHIShaderVisibility visibility = RHIShaderVisibility::All)
    {
        return Table(RHIDescriptorType::Sampler, count, baseRegister, visibility);
    }
}
