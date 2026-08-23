#pragma once
#include <cstdint>

#include "RHIFormat.h"
#include "RHIHandle.h"
#include "RHIPipelineLayout.h"   // RHICompareOp 를 공유한다

// 파이프라인 상태 기술 — 백엔드 중립 (V6).
//
// ── 어휘를 실사용에서 뽑았다 (V1·V4 와 같은 규칙) ──
//
// 패스 전체가 쓰는 값이 놀랄 만큼 적다. 실측:
//
//   채움 2(SOLID·WIREFRAME) · 컬링 2(NONE·BACK) · 토폴로지 2(삼각형·선)
//   블렌드 계수 4(SRC_ALPHA·INV_SRC_ALPHA·ONE·ZERO) · 블렌드 연산 1(ADD)
//   깊이 쓰기 2(ALL·ZERO) · 입력 분류 1(정점별)
//
// ★ D3D12 열거 전체를 옮겨 적지 않는다. 안 쓰는 값을 넣으면 Vulkan 백엔드가
//   그것까지 대응해야 하는데, 그 대응이 맞는지 검증할 자리가 없다 — 쓰는
//   코드가 없으므로 틀려도 아무도 모른다. 필요해지면 그때 는다.
//
// ── 왜 V6 이 여기까지였나, 그리고 A-1 이 어떻게 마저 갔나 ──
//
// V6 은 여기서 멈추며 이렇게 적어 두었다:
//
//   "ID3D12PipelineState* · ID3D12RootSignature* 는 그대로 둔다(실측 65건).
//    그것은 '기술'이 아니라 '만들어진 객체'이고, 불투명 핸들로 바꾸는 것은
//    두 번째 백엔드가 실제로 들어올 때 그 모양을 보고 정할 일이다."
//
// ★ V8-a 에서 그 백엔드가 들어왔고(§7.2.5), 모양이 실측됐다 — 핸들은 짝이어야
//   하고 표를 통해 풀려야 한다. **A-1 이 그 모양대로 잘랐다.**
//
// ★ 그리고 V6 의 멈춤이 값을 했다는 것이 수로 나온다: V8-a 가 세운
//   `VulkanGraphicsPipelineDesc` 와 여기 아래 `RHIGraphicsPipelineDesc` 의
//   전신(`DX12GraphicsPipelineDesc`)이 **21 필드 중 20 이 같았다.**
//   갈린 하나가 객체였고, 그 하나가 A-1 이다.

enum class RHIFillMode : uint8_t
{
    Solid = 0,
    Wireframe,
};

enum class RHICullMode : uint8_t
{
    None = 0,
    Back,
    Front,
};

enum class RHIDepthWrite : uint8_t
{
    All = 0,    ///< 기본. 깊이를 쓴다
    Zero,       ///< 깊이를 보되 쓰지 않는다(데칼이 이것을 쓴다)
};

/// 파이프라인이 조립하는 도형의 **부류**.
///
/// ★ 인코더의 RHIPrimitiveTopology(TriangleList·Strip·LineList…)와 다른
///   개념이다. 이쪽은 파이프라인을 굽는 시점에 정하는 부류이고, 저쪽은
///   드로우마다 정하는 구체적 배열이다. 한 파이프라인으로 List 와 Strip 을
///   둘 다 그릴 수 있으므로 둘은 하나가 될 수 없다.
///   (이름을 겹쳐 두었다가 컴파일러가 잡았다 — 겹쳤으면 조용히 한쪽이
///    가려졌을 자리다.)
enum class RHITopologyType : uint8_t
{
    Triangle = 0,
    Line,
    Point,
};

/// 블렌드 계수. 실사용 넷만 둔다.
enum class RHIBlendFactor : uint8_t
{
    Zero = 0,
    One,
    SrcAlpha,
    InvSrcAlpha,
};

enum class RHIBlendOp : uint8_t
{
    Add = 0,
};

/// 렌더 타깃 하나의 블렌드. MRT 에서 채널마다 다르게 켤 때 쓴다.
///
/// ★ writeMask 0 은 '바인딩돼 있어도 건드리지 않는다'는 뜻이다. 채널을
///   끄려고 타깃을 떼었다 붙였다 할 필요가 없어서 데칼이 이것을 쓴다.
struct RHIRenderTargetBlend
{
    bool           enable{ false };
    RHIBlendFactor srcColor{ RHIBlendFactor::One };
    RHIBlendFactor dstColor{ RHIBlendFactor::Zero };
    RHIBlendOp     colorOp{ RHIBlendOp::Add };
    RHIBlendFactor srcAlpha{ RHIBlendFactor::One };
    RHIBlendFactor dstAlpha{ RHIBlendFactor::Zero };
    RHIBlendOp     alphaOp{ RHIBlendOp::Add };

    /// 채널 쓰기 마스크. 비트 순서는 RGBA(1·2·4·8), 0xF 가 전부 쓴다.
    uint8_t        writeMask{ 0xF };
};

/// 정점 입력 원소 하나.
///
/// ★ semantic 이 const char* 인 것이 해시에서 함정이다 — 구조체를 통째로
///   바이트 해시하면 문자열 **주소**를 해시하게 되어 같은 레이아웃이 매번
///   다른 키가 되고 캐시가 논다. 내용으로 해시해야 한다(루트 시그니처에서
///   같은 것을 겪었다).
struct RHIInputElement
{
    const char* semantic{ nullptr };
    uint32_t    semanticIndex{ 0 };
    RHIFormat   format{ RHIFormat::Unknown };
    uint32_t    inputSlot{ 0 };
    uint32_t    alignedByteOffset{ 0 };

    /// 인스턴스별 데이터면 1 이상(그 값이 스텝 레이트). 0 이면 정점별이다.
    uint32_t    instanceDataStepRate{ 0 };
};

// ── 파이프라인 기술 (A-1) ──
//
// `DX12PSOManager.h` 의 `DX12GraphicsPipelineDesc` 가 여기로 올라온 것이다.
// V6 이 필드의 **어휘**를 이미 중립으로 갈아 두었으므로, A-1 이 한 일은
// 객체 필드 하나를 핸들로 바꾸고 파일을 옮긴 것뿐이다.
//
// ★ `rootSignatureId` 가 사라졌다. 예전에는 desc 가 그것을 따로 받았는데
//   이유가 이랬다:
//
//     "루트 시그니처는 포인터가 실행마다 달라 해시에 못 쓴다. 호출부가
//      안정된 식별자를 주고, 실제 포인터는 생성에만 쓴다."
//
//   **핸들도 실행마다 다르다** — 슬롯+세대라 표에 넣은 순서에 달렸다. 그래서
//   핸들을 그대로 해시에 넣으면 PSO 디스크 캐시(`ID3D12PipelineLibrary`)의
//   키가 매 실행 바뀌어 캐시가 통째로 논다.
//
//   → 표가 **안정 해시를 함께 든다.** 백엔드가 핸들을 그 해시로 풀어서
//     해시한다. 호출부는 이제 아무것도 안 준다 — 손으로 번호를 쓰던 자리가
//     (`DX12RootSignatureCache.h` 가 "자가 검증 코드에 1, 2, 10이 박혀 있다"고
//     적어 둔 그 자리가) 두 단계에 걸쳐 완전히 없어졌다.
//
//   ★ 이 판단의 판정 도구는 `dx12.psocache` 다 — "2회차 컴파일 0건"을 본다.

struct RHIGraphicsPipelineDesc
{
    // 셰이더는 내용으로 해시하므로 셰이더가 바뀌면 자동으로 다른 키가 된다 —
    // 핫리로드와 캐시 무효화가 같은 메커니즘으로 처리된다.
    const void* vsBytecode{ nullptr };
    size_t      vsSize{ 0 };
    const void* psBytecode{ nullptr };
    size_t      psSize{ 0 };

    /// 이 파이프라인이 구워질 레이아웃. 캐시가 이것을 기억해 두었다가
    /// `SetPipeline` 때 파이프라인과 **짝으로** 돌려준다(RHIHandle.h ★).
    RHIPipelineLayoutHandle layout;

    RHIFillMode fillMode{ RHIFillMode::Solid };
    RHICullMode cullMode{ RHICullMode::None };
    bool        depthEnable{ false };
    bool        blendEnable{ false };

    // 깊이 쓰기와 비교 함수. 나눠 둔 이유: 데칼처럼 깊이를 '보되 쓰지 않는'
    // 패스가 있다. depthEnable 하나로는 그 구분이 안 되고, 쓰기를 못 막으면
    // 데칼 상자가 깊이 버퍼를 덮어써 뒤따르는 패스가 엉뚱한 깊이를 읽는다.
    RHIDepthWrite depthWriteMask{ RHIDepthWrite::All };
    RHICompareOp  depthFunc{ RHICompareOp::Less };

    // ── 렌더타깃별 독립 블렌드 ──
    //
    // 끄면(기본) blendEnable이 RenderTarget[0]의 고정 조합(SRC_ALPHA/
    // INV_SRC_ALPHA)을 켜고 모든 타깃에 적용한다.
    //
    // 켜면 renderTargetBlend를 그대로 쓴다. 마스크가 0인 타깃은 바인딩돼
    // 있어도 건드리지 않는다는 뜻이라, 채널을 끄려고 타깃을 떼었다 붙였다 할
    // 필요가 없다(데칼이 확산·노멀·ORM을 따로 켜고 끈다).
    bool independentBlend{ false };
    RHIRenderTargetBlend renderTargetBlend[8]{};

    // 입력 레이아웃. 호출부가 소유하고 이 구조체는 참조만 든다 —
    // GetOrCreate가 돌아올 때까지만 살아 있으면 된다. 해시는 반드시 내용으로
    // 해야 한다(RHIInputElement의 ★ 참고).
    const RHIInputElement* inputElements{ nullptr };
    uint32_t               inputElementCount{ 0 };

    RHITopologyType topologyType{ RHITopologyType::Triangle };
    uint32_t        numRenderTargets{ 1 };
    RHIFormat       rtvFormats[8]{ RHIFormat::RGBA8Unorm };
    RHIFormat       dsvFormat{ RHIFormat::Unknown };
    uint32_t        sampleCount{ 1 };
};

/// 컴퓨트 파이프라인. 그래픽과 같은 해시 공간을 쓰되 백엔드가 태그로 구분해,
/// 우연히 같은 바이트 배열이 나와도 두 종류가 섞이지 않게 한다.
struct RHIComputePipelineDesc
{
    const void* csBytecode{ nullptr };
    size_t      csSize{ 0 };

    RHIPipelineLayoutHandle layout;
};

