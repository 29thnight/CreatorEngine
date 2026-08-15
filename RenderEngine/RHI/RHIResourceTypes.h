#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

#include "RHIFormat.h"
#include "RHIHandle.h"
#include "RHIResourceState.h"

/// 자산 텍스처 캐시가 패스에 돌려주는 중립 설명.
///
/// 캐시는 GPU 리소스의 소유권을 계속 들고, 패스는 이 값으로 그 프레임의
/// 뷰만 요청한다. 포맷을 RHI 어휘로 두어 DX12/Vulkan 캐시가 같은
/// `IRenderTextureCache`를 구현할 수 있게 한다.
struct RHITextureEntry
{
    RHITextureHandle handle;
    RHIFormat        format{ RHIFormat::Unknown };
    uint32_t         width{ 0 };
    uint32_t         height{ 0 };
    uint32_t         mipLevels{ 0 };
    uint32_t         arraySize{ 1 };
    bool             isCube{ false };

    bool IsValid() const { return handle.IsValid(); }
};

// 패스와 백엔드가 주고받는 값 타입 (V7 의 필요분, A 완료 5a).
//
// ── 왜 파일을 가르는가 ──
//
// 여기 있는 것은 전부 `RenderFrameServices.h`(RHI/DX12/) 안에 있었고, 전부
// **이미 중립**이었다 — 핸들·열거·POD 뿐이고 D3D12 타입이 하나도 없다.
// 그런데 그 헤더가 `d3d12.h` 를 물기 때문에, 내용이 중립인 것과 무관하게
// **Vulkan 이 이 타입들을 쓸 수가 없었다.**
//
// ★ 증거가 저장소에 남아 있었다. `VulkanEncoder.h` 가 `RHIBindPoint` 와
//   `RHIPrimitiveTopology` 를 **글자까지 같게 베껴** 두고 그 이유를 적어
//   놨다: "이미 중립인 어휘가 DX12 헤더 안에 갇혀 있는 것".
//
// ★ 그래서 이 파일은 **옮기는 것이 아니라 원래 갈려 있던 것을 인정하는
//   것**이다. 한 줄도 고치지 않고 그대로 왔다(`RHIReadback` 만 예외 —
//   아래 ★ 참고). `RenderFrameServices.h` 가 이것을 include 하므로 기존
//   소비처 41곳은 한 줄도 안 바뀐다.
//
// ── 계획서가 이 순서를 틀렸다 ──
//
// §8.5 는 이 이동(V7)을 9번, 맨 마지막에 뒀다. "이동이라 값이 적다"로
// 읽었기 때문인데, 실측하면 **이것 없이는 5번(두 백엔드가 같은 패스를
// 돈다)이 성립하지 않는다.** 작업의 크기로 값을 매긴 것이고, 목표로 재면
// 이동이 아니라 **공유의 전제**다(RhiBoundaryPlan.md §7.2.14 ②).

// ── 바인딩 (R2) ──
//
// 지금까지 패스마다 "링에서 N개 자르고 → 뷰를 하나씩 만들고 → 테이블을 건다"를
// 손으로 했다. 그것이 결합의 최대 덩어리이자(뷰 생성 66 · 테이블 33 · 힙 20)
// 가장 조용히 틀리는 자리다:
//
//   · 링이 모자라면 Allocate가 invalid를 주는데 검사를 빠뜨리면 널 핸들에 쓴다
//   · 깊이(D32_FLOAT)를 SRV로 읽으려면 포맷을 R32_FLOAT로 명시해야 하는데,
//     빠뜨리면 nullptr 설명으로 만들려다 조용히 실패한다
//   · 배열·큐브·3D는 ViewDimension을 손으로 맞춰야 한다
//
// 셋 다 한 곳으로 모은다. 패스는 "무엇을 몇 번 슬롯에 걸지"만 적는다.

/// 테이블에 걸 뷰 하나. 정적 팩토리로 만든다 — 필드를 직접 채우면
/// 위 실수들이 그대로 돌아온다.
struct RHIBindingDesc
{
    enum class Kind : uint8_t { ShaderResource, UnorderedAccess };
    enum class Dim  : uint8_t { Default, Texture2D, Texture2DArray, TextureCube, Texture3D, Buffer };

    Kind             kind{ Kind::ShaderResource };
    Dim              dim{ Dim::Default };

    // ★ 포인터에서 핸들로 (V2-b). 뷰를 만드는 일은 백엔드의 몫이고, 그
    //   백엔드가 Vulkan이면 여기 들어갈 것이 VkImage + VkImageView +
    //   VkDeviceMemory다 — 포인터 하나로는 못 가리킨다.
    //
    //   텍스처와 버퍼를 두 칸으로 나눈 것은 Dim::Buffer만 뒤를 쓰기 때문이다.
    //   한 칸에 몰면 "버퍼를 큐브맵으로 봤다"가 컴파일된다.
    RHITextureHandle resource;
    RHIBufferHandle  bufferResource;

    /// UNKNOWN이면 리소스가 스스로 아는 포맷을 쓴다.
    RHIFormat       format{ RHIFormat::Unknown };

    /// 깊이 리소스를 색 채널로 갈아 본다(D32_FLOAT → R32_FLOAT 따위).
    /// 매핑은 CreateBindings 한 곳에만 있다 — SrvDepth 참고.
    bool            depthAsColor{ false };

    /// 리소스가 없어도 그 자리에 유효한 널 디스크립터를 깐다. OrNull() 참고.
    bool            allowNull{ false };

    uint32_t mostDetailedMip{ 0 };
    uint32_t mipLevels{ 1 };
    uint32_t firstSlice{ 0 };
    uint32_t sliceCount{ 1 };

    // 버퍼 UAV용
    uint32_t firstElement{ 0 };
    uint32_t numElements{ 0 };
    uint32_t structureByteStride{ 0 };

    /// 리소스가 스스로 아는 대로 본다(nullptr 설명과 같다). 가장 흔한 경우.
    static RHIBindingDesc Srv(RHITextureHandle resource)
    {
        RHIBindingDesc d{}; d.resource = resource; return d;
    }

    /// 포맷을 갈아서 본다. 깊이 버퍼를 셰이더로 읽는 자리가 이것이다.
    static RHIBindingDesc Srv2D(RHITextureHandle resource, RHIFormat format,
        uint32_t mostDetailedMip = 0, uint32_t mipLevels = 1)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture2D; d.resource = resource; d.format = format;
        d.mostDetailedMip = mostDetailedMip; d.mipLevels = mipLevels;
        return d;
    }

    /// 깊이 버퍼를 셰이더로 읽는다.
    ///
    /// ★ 이 자리가 R2가 없애려던 조용한 실수의 대표다. 깊이는 D32_FLOAT로
    ///   만들어지는데 그 포맷으로는 SRV를 만들 수 없어서, 읽는 쪽이 매번
    ///   R32_FLOAT를 손으로 적어야 했다. 빠뜨리면 뷰 생성이 조용히 실패하고
    ///   화면에는 'AO가 안 걸린다' 정도로만 나타난다.
    ///
    ///   깊이 포맷은 넷뿐이고 대응하는 색 포맷도 정해져 있다. 호출부가 알
    ///   이유가 없어 CreateBindings로 내렸다 — 여기서는 "깊이를 읽는다"만 적는다.
    static RHIBindingDesc SrvDepth(RHITextureHandle resource)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture2D; d.resource = resource; d.depthAsColor = true;
        return d;
    }

    static RHIBindingDesc SrvArray(RHITextureHandle resource, RHIFormat format,
        uint32_t sliceCount, uint32_t firstSlice = 0)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture2DArray; d.resource = resource; d.format = format;
        d.firstSlice = firstSlice; d.sliceCount = sliceCount;
        return d;
    }

    static RHIBindingDesc SrvCube(RHITextureHandle resource, RHIFormat format,
        uint32_t mipLevels = 1)
    {
        RHIBindingDesc d{};
        d.dim = Dim::TextureCube; d.resource = resource; d.format = format;
        d.mipLevels = mipLevels;
        return d;
    }

    static RHIBindingDesc Srv3D(RHITextureHandle resource, RHIFormat format)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture3D; d.resource = resource; d.format = format;
        return d;
    }

    static RHIBindingDesc Uav2D(RHITextureHandle resource,
        RHIFormat format = RHIFormat::Unknown, uint32_t mipSlice = 0)
    {
        RHIBindingDesc d{};
        d.kind = Kind::UnorderedAccess; d.dim = Dim::Texture2D;
        d.resource = resource; d.format = format; d.mostDetailedMip = mipSlice;
        return d;
    }

    static RHIBindingDesc Uav3D(RHITextureHandle resource, RHIFormat format,
        uint32_t sliceCount)
    {
        RHIBindingDesc d{};
        d.kind = Kind::UnorderedAccess; d.dim = Dim::Texture3D;
        d.resource = resource; d.format = format; d.sliceCount = sliceCount;
        return d;
    }

    /// "이 자리는 비어 있어도 된다" — 리소스가 없으면 널 디스크립터를 깐다.
    ///
    /// ★ 널 디스크립터는 '안 거는 것'과 다르다. 링에서 잘라 온 자리는
    ///   초기화되지 않은 쓰레기라, 셰이더가 분기로 안 읽더라도 유효한
    ///   디스크립터가 있어야 한다 — SSGI에서 쓰레기 디스크립터로 GPU가 죽었다.
    ///
    ///   그래서 기본은 '널이면 테이블 전체를 거절'이고(실수 잡기), 비어도
    ///   되는 자리만 여기에 표시해 예외로 둔다. 표시한 자리는 포맷과 차원을
    ///   반드시 적어야 한다 — 리소스에게 물어볼 수 없기 때문이다.
    RHIBindingDesc OrNull() const
    {
        RHIBindingDesc d = *this; d.allowNull = true; return d;
    }

    static RHIBindingDesc UavBuffer(RHIBufferHandle resource, uint32_t numElements,
        uint32_t structureByteStride, uint32_t firstElement = 0)
    {
        RHIBindingDesc d{};
        d.kind = Kind::UnorderedAccess; d.dim = Dim::Buffer; d.bufferResource = resource;
        d.numElements = numElements; d.structureByteStride = structureByteStride;
        d.firstElement = firstElement;
        return d;
    }
};

/// 잘라 둔 연속 테이블. 수명은 발급한 recording의 completion까지다.
///
/// ★ 아직 D3D12 GPU 핸들을 그대로 들고 있다. 패스가 이것을
///   SetGraphicsRootDescriptorTable에 넘기기 때문이고, 그 호출이 R3에서
///   인코더로 옮겨 가면 이 타입도 불투명 핸들이 된다.
struct RHIBindingTable
{
    /// 백엔드가 뜻을 주는 불투명 값 (A-5b).
    ///
    /// ★ DX12 는 `D3D12_GPU_DESCRIPTOR_HANDLE::ptr`(recording 전용 page의 GPU
    ///   핸들)다. Vulkan 은 CreateBindings 때 셋 레이아웃을 아직 모르므로 현재
    ///   요청 epoch의 정수 슬롯이다. SetBindings가 현재 레이아웃과 합쳐 그때
    ///   `VkDescriptorSet`을 만든다. 어느 쪽이든 상위에는 불투명 정수로 족하다.
    ///
    /// ★ V8-b 가 "테이블 하나가 Vulkan 에서 binding N개로 펼쳐진다"고 실측한
    ///   것은 **레이아웃**의 이야기이고(`VulkanPipelineCache` 가 그 펼침을
    ///   한다), 걸 때 넘기는 것은 양쪽 다 하나다. 그래서 모델 교체의 실체가
    ///   "인덱스 셋 → 뷰 목록"(렌더 타깃 쪽)이 아니라 "타입만 불투명하게"다 —
    ///   자를 세우고 나서야 이 둘이 다른 부류라는 것이 보였다.
    uint64_t backend{ 0 };
    uint32_t count{ 0 };

    /// transient descriptor가 어느 backend version에서 발급됐는지 나타낸다.
    /// DX12는 shader-visible page의 slot+generation이며, Vulkan은 CPU 요청 표의
    /// epoch다. 0은 version이 발급되지 않은 무효 transient table이다.
    uint64_t version{ 0 };

    bool IsValid() const { return 0 != count; }
};

/// 샘플러 테이블 (R3-2).
///
/// ★ 같은 불투명 64비트인데 왜 RHIBindingTable을 안 쓰는가:
///   샘플러는 **다른 힙**에서 온다. SRV 테이블 핸들을 샘플러 슬롯에 걸면
///   컴파일도 되고 검증 레이어도 대개 지나가지만 GPU에서 잘못 읽는다.
///   타입을 갈라 두면 그 실수가 표현 불가능해진다 — R2b의
///   RHIRenderTargetBinding이 핸들 대신 인덱스를 든 것과 같은 이유다.
///
///   ★ Vulkan에서도 이 구분이 산다. 샘플러는 `VK_DESCRIPTOR_TYPE_SAMPLER`로
///     풀 예산이 따로이고(V8-b 실측), 정적 샘플러면 셋 레이아웃에 구워진다.
///
/// ★ R3는 "거는 동작"만 맡고 만드는 쪽(D3D12_SAMPLER_DESC)은 DX12로 남겨
///   두었다 — 샘플러 설명의 중립화가 필터·주소 모드·비교 함수를 전부
///   옮기는 별개의 몫이었기 때문이다. **V4가 그 몫을 끝냈다**:
///   DX12SamplerHeap 은 이제 RHISamplerDesc 를 받고, 대응표는 정적 샘플러와
///   한 벌로 DX12PipelineLayoutTranslate.h 에 있다.
///
///   남은 DX12는 이 구조체가 든 핸들 자체뿐이고, 그것은 디스크립터 힙
///   모델이 갈리는 자리라 백엔드 골격(V8)이 볼 몫이다.
struct RHISamplerTable
{
    /// 〃. 샘플러 힙은 CBV/SRV/UAV 힙과 다른 힙이라 타입을 가른다(아래 ★).
    uint64_t backend{ 0 };

    bool IsValid() const { return 0 != backend; }
};

// ── 렌더 타깃 (R2b) ──
//
// R2a가 셰이더 가시 테이블(SRV/UAV)을 걷었고, 여기가 그 나머지다. 성격이
// 달라 인터페이스도 다르다: RTV/DSV는 shader-visible recycler가 아니라 **패스마다 자기
// 힙**에서 왔다. 헤더 14개가 ComPtr<ID3D12DescriptorHeap>을 들고 Initialize에서
// 만들고 Shutdown에서 놓았으며, 매 프레임 같은 칸에 뷰를 다시 만들었다.
//
// ★ 이 반복이 R2a가 없앤 것보다 조용히 틀리기 쉽다. 깊이가 그렇다 —
//   D32_FLOAT 리소스에 DSV를 만들 때 포맷을 안 적으면 어긋나고, 배열이면
//   ViewDimension과 슬라이스를 손으로 맞춰야 하며, 깊이를 읽으면서 묶는
//   패스는 READ_ONLY 플래그를 빠뜨리면 그래프가 계획한 상태와 어긋난다.
//   R2a의 SrvDepth가 '읽는 쪽'에서 없앤 것과 같은 부류가 '쓰는 쪽'에 남아
//   있었다.

/// 깊이 타깃 하나. 색 타깃과 달리 설명이 필요해서 구조체다 —
/// 색은 지금 전 사이트가 "리소스가 아는 대로"(nullptr 설명)라 포인터면 충분하다.
struct RHIDepthTargetDesc
{
    RHITextureHandle resource;

    /// 필수다. 깊이는 리소스 포맷 그대로 뷰를 만들 수 없는 경우가 있어
    /// (D32_FLOAT_S8X24_UINT 같은 것) 호출부가 무엇으로 볼지 정해야 한다.
    RHIFormat       format{ RHIFormat::Unknown };

    /// 깊이를 셰이더로 읽으면서 동시에 묶을 때 켠다. 그래프가 DepthRead로
    /// 선언한 자리의 계약이고(EnhancedRenderGraph.h), 빠뜨리면 상태 전이와
    /// 어긋나 검증 레이어가 잡는다.
    bool            readOnly{ false };

    /// sliceCount가 0이면 Texture2D, 1 이상이면 Texture2DArray로 본다.
    /// 그림자 캐스케이드가 배열 한 장씩 묶는 자리다.
    uint32_t        firstSlice{ 0 };
    uint32_t        sliceCount{ 0 };

    static RHIDepthTargetDesc Depth(RHITextureHandle resource, RHIFormat format)
    {
        RHIDepthTargetDesc d{}; d.resource = resource; d.format = format; return d;
    }

    static RHIDepthTargetDesc DepthReadOnly(RHITextureHandle resource, RHIFormat format)
    {
        RHIDepthTargetDesc d{}; d.resource = resource; d.format = format;
        d.readOnly = true; return d;
    }

    static RHIDepthTargetDesc DepthSlice(RHITextureHandle resource, RHIFormat format,
        uint32_t slice)
    {
        RHIDepthTargetDesc d{}; d.resource = resource; d.format = format;
        d.firstSlice = slice; d.sliceCount = 1; return d;
    }
};

/// 리소스가 스스로 아는 것 (5c-1).
///
/// ★ **핸들만 들고 있을 때 "이게 뭐였지"를 되묻는 길이다.** 지금까지 그
///   질문의 답은 `Resolve(handle)->GetDesc()` 뿐이었고, 그래서 패스 셋이
///   포인터로 풀고 있었다(SSGI 2 · PostChain 1). 코드가 스스로 적어 놓기도
///   했다 — "포맷은 리소스가 안다 — 핸들만 있으므로 서비스에 되묻는다".
///
///   되묻는 길이 포인터밖에 없어서 남은 자리이고, 양쪽 백엔드가 똑같이
///   답할 수 있는 질문이다(`D3D12_RESOURCE_DESC` ↔ 이미지 생성 정보).
///
/// 무효 핸들이면 전부 0 · `Unknown` 이다.
struct RHITextureInfo
{
    uint32_t  width{ 0 };
    uint32_t  height{ 0 };
    uint32_t  depthOrArraySize{ 0 };
    uint32_t  mipLevels{ 0 };
    RHIFormat format{ RHIFormat::Unknown };

    bool IsValid() const { return 0 != width && 0 != height; }
};

/// 만들어 둔 렌더 타깃 묶음. 수명은 이 프레임이다.
///
/// ★ **불투명 값이 됐다 (5b).** 예전에는 `rtvIndex`·`dsvIndex` — 프레임 힙
///   안의 인덱스 — 였고, 그 모델이 Vulkan 에 없다는 것이 `VulkanEncoder.h`
///   가 상속을 미룬 이유 중 하나였다(동적 렌더링은 이미지 뷰를 커맨드에
///   직접 받는다).
///
/// ★ **그 진단은 절반만 맞았다.** 저 헤더는 "인덱스 셋을 핸들 하나로가
///   아니라 **인덱스 모델을 버리고 뷰 목록으로**" 여야 한다고 적었는데,
///   실측하면 **패스 10곳이 `IsValid()` 하나만 읽는다** — 필드를 보는 것은
///   백엔드 자신뿐이다. 즉 뷰 목록이 필요한 것은 Vulkan 구현 **안쪽**이고,
///   계약이 나를 것은 "무엇인지 모를 값 하나 + 셀 수 있는 개수"다.
///
///   A-5b 가 `RHIBindingTable` 에서 한 정정과 **같은 것이 두 번째**다:
///   백엔드 안에서 모양이 다른 것과 계약의 모양이 달라야 하는 것은 다르다.
///
/// 백엔드가 뜻을 준다 — DX12 는 RTV·DSV 힙 인덱스 두 개를 한 값에 담고
/// (`DX12PackTargets`), Vulkan 은 프레임 뷰 묶음의 슬롯을 담는다.
struct RHIRenderTargetBinding
{
    uint64_t backend{ 0 };

    /// 계약에 남는다 — 클리어가 개수만큼 돌아야 하고, Vulkan 도
    /// `colorAttachmentCount` 로 같은 수를 요구한다.
    uint32_t colorCount{ 0 };
    bool     hasDepth{ false };

    bool HasColor() const { return 0 != colorCount; }
    bool HasDepth() const { return hasDepth; }

    /// ★ `backend` 로 판정하지 않는다. 인덱스 0 이 유효한 값이라 0 을
    ///   무효로 쓸 수 없다 — `RHIBindingTable` 이 `count` 로 판정하는 것과
    ///   같은 이유다.
    bool IsValid()  const { return HasColor() || HasDepth(); }
};

// ── 패스가 소유하는 리소스 (R2c) ──
//
// 그래프는 트랜지언트만 소유한다. 프레임을 넘겨 값을 잇는 것 — Forward+의
// 타일 버퍼, SSGI 히스토리, VolFog 볼륨 — 은 패스가 직접 만든다. 그래서
// 뷰를 다 걷어낸 뒤에도 CreateCommittedResource가 프로덕션에 남아 있었다.
//
// ★ 여기서 없애는 것은 뷰 때와 성격이 다르다. R2a·R2b는 같은 손코드가
//   패스마다 반복되는 것을 걷었지만, 리소스 생성은 세 곳뿐이라 줄어드는
//   줄 수가 크지 않다. 그런데도 옮기는 값이 셋 있다:
//
//   · 조용히 틀리는 기본값이 한곳으로 모인다. SampleDesc.Count를 빠뜨리면
//     desc가 무효가 되고, 버퍼에 Layout=ROW_MAJOR를 안 주면 생성이 실패한다.
//     둘 다 호출부가 매번 손으로 채우던 값이다.
//   · 이름이 규약이 된다. 지금은 VolFog만 SetName을 부르고 나머지는 이름이
//     없어서, PIX 캡처와 DRED 숨결에 정체 모를 리소스로 뜬다. desc의 필드로
//     두면 만들면서 이름을 정하지 않을 수가 없다.
//   · 프로덕션 경로에서 GetDevice가 사라진다. 남는 것은 VolFog의 클리어 힙
//     하나뿐이고 그것은 R3에서 인코더와 함께 닫힌다.
//
// ★ 초기 상태가 RHIResourceState다(A-2). 원래 D3D12_RESOURCE_STATES였고,
//   미뤄 둔 이유는 "그 타입이 그래프 헤더에 있어 경계 헤더가 그래프를 끌어오는
//   것은 방향이 거꾸로"였다. V3가 RHIResourceState를 RHI/로 올리면서 그 조건이
//   사라졌다 — 미루면서 조건을 적어 둔 덕에 다시 판단하지 않고 충족만
//   확인하면 됐다(V2-a가 세대를 미룬 것과 같은 방식).

/// 업로드 또는 패스 소유 버퍼에서 잘라 낸 조각 (A-5a).
///
/// ★ **왜 GPU 주소 하나가 아닌가.** DX12 는 `D3D12_GPU_VIRTUAL_ADDRESS` 하나로
///   되지만 Vulkan 은 `{VkBuffer, offset}` 가 필요하다 — 버퍼 디바이스 주소는
///   확장이고, 디스크립터에 거는 표준 경로가 버퍼+오프셋이다. 한쪽에만
///   코어인 것을 계약에 넣지 않는다(§7.2.2 가 `QueryVideoMemory::usedMB` 에서
///   같은 판단을 했다).
///
/// ★ **`SubRange` 가 성능 계약이다.** 한 번 예약한 범위를 여러 조각으로
///   나누는 패턴을 타입이 직접 제공한다. 이 타입은 특정 링 버퍼 구현이나
///   프레임 슬롯의 수명 모델을 뜻하지 않는다.
struct RHIBufferSlice
{
    RHIBufferHandle buffer;              ///< 실제 버퍼. 백엔드가 이것으로 푼다
    uint64_t        offset{ 0 };
    uint64_t        size{ 0 };
    void*           cpuAddress{ nullptr };   ///< 쓰는 쪽. 링은 계속 매핑돼 있다

    /// 걸 수 있는가. **CPU 주소가 없어도 유효하다** — 패스가 소유한 버퍼를
    /// 통째로 가리키는 슬라이스는 쓰는 쪽이 없다(`Whole` 참고).
    bool IsValid() const { return buffer.IsValid(); }

    /// CPU 로 쓸 수 있는가. 링에서 자른 것만 참이다.
    bool IsWritable() const { return nullptr != cpuAddress; }

    /// 버퍼 전체를 가리킨다 (A-5a).
    ///
    /// ★ 슬라이스가 '링 조각'만 뜻하지 않는 이유가 이것이다. 패스 소유 버퍼
    ///   (Forward+ 의 타일 카운트·타일 목록)도 루트에 걸리는데, 예전에는
    ///   `Resolve(handle)->GetGPUVirtualAddress()` 로 호출부가 직접 풀었다.
    ///   같은 자리에 두 어휘가 있으면 한쪽만 중립화한 것이 된다.
    static RHIBufferSlice Whole(RHIBufferHandle buffer)
    {
        RHIBufferSlice slice{};
        slice.buffer = buffer;
        return slice;
    }

    /// 블록 안의 부분 조각. 원본이 유효할 때만 뜻이 있다.
    RHIBufferSlice SubRange(uint64_t byteOffset, uint64_t bytes) const
    {
        RHIBufferSlice sub{};
        if (!IsWritable() || byteOffset + bytes > size) return sub;
        sub.buffer = buffer;
        sub.offset = offset + byteOffset;
        sub.size = bytes;
        sub.cpuAddress = static_cast<uint8_t*>(cpuAddress) + byteOffset;
        return sub;
    }
};

/// 업로드 바이트의 의미. 호출부는 네이티브 정렬값 대신 용도를 말하고,
/// 백엔드는 장치 제한과 minimumAlignment 중 큰 값을 적용한다.
enum class RHIUploadUsage : uint8_t
{
    Raw,
    ConstantBuffer,
    VertexData,
    IndexData,
    BufferCopy,
    TextureCopy,
    ShaderTable
};

struct RHIUploadRequest
{
    uint64_t       bytes{ 0 };
    RHIUploadUsage usage{ RHIUploadUsage::Raw };
    uint64_t       minimumAlignment{ 1 };
};

/// 양쪽 backend가 같은 의미로 받는 transient segment 정책.
/// soft budget은 하드 실패선이 아니라 available cache를 먼저 줄이는 압력선이다.
struct RHIUploadSegmentPolicy
{
    uint64_t regularSegmentBytes{ 16ull * 1024 * 1024 };
    uint64_t largeThreshold{ 8ull * 1024 * 1024 };
    uint64_t softBudgetBytes{ 256ull * 1024 * 1024 };
    uint64_t largeCacheBudgetBytes{ 64ull * 1024 * 1024 };
    uint32_t standbyRegularSegments{ 3 };
    uint32_t trimDelayCollects{ 8 };
};

/// backend 예산 API에서 얻은 upload memory heap의 순간값.
struct RHIUploadMemoryBudget
{
    uint64_t usageBytes{ 0 };
    uint64_t budgetBytes{ 0 };
    bool estimated{ false };

    bool IsValid() const { return 0 != budgetBytes; }
};

/// graphics queue 하나에서 쓰는 단조 증가 완료점. queue가 갈라지면
/// queue id 또는 완료점 집합으로 확장해야 하며 서로 다른 queue에 섞어 쓰지 않는다.
struct RHICompletionPoint
{
    uint64_t value{ 0 };
    bool IsValid() const { return 0 != value; }
};

enum class RHIUploadSegmentState : uint8_t
{
    Available,
    Active,
    Pending,
    Quarantined
};

enum class RHIUploadTransactionState : uint8_t
{
    Recording,
    Queued,
    Resident,
    Quarantined
};

/// DX12와 Vulkan이 같은 의미로 내는 transient upload 관측값.
struct RHIUploadStats
{
    // 이행 기간의 누적 계수. 새 진단은 아래 상태별 수치를 우선 사용한다.
    uint64_t allocations{ 0 };
    uint64_t bytesAllocated{ 0 };
    uint64_t overflows{ 0 };
    uint64_t peakFrameBytes{ 0 };
    uint32_t segmentCount{ 0 };
    uint64_t segmentBytes{ 0 };
    uint32_t growths{ 0 };
    uint32_t activeSegments{ 0 };
    uint64_t activeBytes{ 0 };
    uint32_t pendingSegments{ 0 };
    uint64_t pendingBytes{ 0 };
    uint32_t availableSegments{ 0 };
    uint64_t availableBytes{ 0 };
    uint32_t largeSegments{ 0 };
    uint64_t largeBytes{ 0 };
    uint64_t peakRecordingBytes{ 0 };
    uint64_t slowPathCreates{ 0 };
    uint64_t reuses{ 0 };
    uint64_t fastPathReservations{ 0 };
    uint64_t slowPathReservations{ 0 };
    uint64_t casRetries{ 0 };
    uint64_t workerSegmentCreates{ 0 };
    uint64_t tailWasteBytes{ 0 };
    uint64_t batchRollbacks{ 0 };
    uint64_t oomFailures{ 0 };
    uint64_t oldestPendingValue{ 0 };
    uint64_t reclaimLag{ 0 };
    uint64_t softBudgetBytes{ 0 };
    uint64_t largeCacheBudgetBytes{ 0 };
    uint64_t trimmedSegments{ 0 };
    uint64_t trimmedBytes{ 0 };
    uint64_t budgetPressureEvents{ 0 };
    uint64_t budgetRetries{ 0 };
    uint64_t budgetOvercommits{ 0 };
    uint64_t registrySlotReuses{ 0 };
    uint32_t registryHighWater{ 0 };
};

struct RHIBufferDesc
{
    uint64_t bytes{ 0 };
    bool     allowUnorderedAccess{ false };

    /// ★ 버퍼는 Common으로 만들고 첫 사용에서 승격시키는 것이 맞다.
    ///   UnorderedAccess를 초기 상태로 주면 검증 레이어가 '무시한다'고
    ///   경고만 남기고 지나간다 — 틀렸는데 조용한 부류라 기본값으로 박아 둔다.
    RHIResourceState initialState{ RHIResourceState::Common };

    /// PIX·DRED에서 읽히는 이름. 비우지 말 것.
    const wchar_t* debugName{ nullptr };
};

struct RHITextureDesc
{
    enum class Dim : uint8_t { Texture2D, Texture3D };

    Dim         dim{ Dim::Texture2D };
    uint32_t    width{ 0 };
    uint32_t    height{ 0 };

    /// 2D면 배열 길이, 3D면 깊이. 1이 기본이다.
    uint32_t    depthOrArraySize{ 1 };
    uint32_t    mipLevels{ 1 };
    RHIFormat   format{ RHIFormat::Unknown };

    bool        allowUnorderedAccess{ false };
    bool        allowRenderTarget{ false };

    /// 깊이·스텐실 타깃으로 쓴다 (G-1).
    ///
    /// ★ 그래프의 트랜지언트가 유일한 소비자다. 그래프가 이 desc 를 못 써서
    ///   `CreateCommittedResource` 를 손으로 부르던 이유 둘 중 하나였고,
    ///   그래서 `ID3D12Device` 가 그래프 헤더에 남아 있었다.
    bool        allowDepthStencil{ false };

    /// 최적 클리어 값. 렌더 타깃이나 깊이 타깃일 때만 뜻이 있다 (G-1).
    ///
    /// ★ **비워 두는 것이 안전한 기본이 아니다.** 힌트를 안 주면 검증
    ///   레이어가 경고를 남기고(3-3 실측), 실제 클리어가 힌트와 다른 값을
    ///   써도 경고한다. 이 저장소는 WARNING 을 실패로 세므로(§7.2.11 ★★)
    ///   둘 다 검사를 깬다. 그래서 기본값을 두고 호출부가 맞춘다 —
    ///   깊이는 1.0(먼 평면), 색은 검정.
    ///
    /// ★ Vulkan 에는 이 개념이 없다(클리어 값을 렌더패스 시작에 준다).
    ///   한쪽에만 **최적화**인 것이라 무시하면 되고, 계약이 깨지지 않는다 —
    ///   한쪽에만 **코어**인 것을 뺀 `QueryVideoMemory::usedMB`(§7.2.2)와
    ///   방향이 반대인 판단이다.
    float       clearColor[4]{ 0.f, 0.f, 0.f, 0.f };
    float       clearDepth{ 1.f };

    RHIResourceState initialState{ RHIResourceState::Common };
    const wchar_t* debugName{ nullptr };
};

// ── 리드백 (R2c-b) ──
//
// 자가 검증이 GPU가 그린 것을 CPU에서 재려면 리드백이 필요하다. 그 배관이
// 검사마다 통째로 복제돼 있었다 — 세어 보면 Map/Unmap 81 · 행 피치 계산 63 ·
// CopyTextureRegion 53 · READBACK 힙 32, 파일 28개.
//
// ★ 그런데 더 큰 중복은 배관이 아니라 '읽는 법'이었다:
//
//     XxxHalfToFloat  17종 — 이름만 다르고 본문이 바이트 단위로 같다
//     kXxxRowPitch    18종 — 같은 정렬 계산
//     XxxCapture      8종+ — data + At(x, y, channel)이 공통
//
//   같은 것을 열일곱 번 적어 둔 자리다. 그래서 캡처 타입이 포맷을 알게 한다 —
//   At이 포맷을 보고 디코드하면 위 셋이 함께 사라진다.
//
// 쓰는 순서는 GPU 리드백의 성질이 정한다. 세 단계가 서로 다른 시점이라
// 한 함수로 묶을 수 없다:
//
//   CreateReadback   프레임 밖 — 대상 버퍼를 만든다
//   CopyToReadback   기록 시점 — 커맨드에 복사를 넣는다
//   MapReadback      제출·대기 뒤 — 값을 읽는다

/// CPU로 읽어 온 이미지. 포맷을 알고 있어 호출부가 디코더를 다시 만들지 않는다.
struct RHIReadbackImage
{
    std::vector<uint8_t> data;
    uint32_t    width{ 0 };
    uint32_t    height{ 0 };
    uint32_t    rowPitch{ 0 };

    /// ★ V1에서 DXGI_FORMAT을 걷었다. 디코드가 백엔드 열거에 묶여 있으면
    ///   Vulkan 백엔드가 같은 캡처 타입을 못 쓴다 — 읽는 값은 API와 무관한데
    ///   읽는 방법만 API를 알던 자리다.
    RHIFormat   format{ RHIFormat::Unknown };

    /// 여러 장을 한 버퍼에 담은 경우(데칼은 확산·노멀·ORM 셋을 한 번에 읽는다).
    uint32_t sliceCount{ 1 };
    size_t   sliceBytes{ 0 };

    bool IsValid() const { return !data.empty() && 0 != width && 0 != height; }

    /// 픽셀 하나의 채널 값을 0~1(또는 float 원값)로 돌려준다.
    ///
    /// 범위 밖이면 0을 준다 — 검사가 경계를 훑는 일이 많고, 거기서 죽는 것보다
    /// 0이 낫다(0이 나오면 그 자체가 눈에 띈다).
    float At(uint32_t x, uint32_t y, uint32_t channel, uint32_t slice = 0) const
    {
        if (x >= width || y >= height || channel >= 4 || slice >= sliceCount) return 0.f;

        const size_t offset = static_cast<size_t>(slice) * sliceBytes
            + static_cast<size_t>(y) * rowPitch;
        if (offset >= data.size()) return 0.f;

        const uint8_t* row = data.data() + offset;

        switch (format)
        {
        case RHIFormat::RGBA16Float:
            return DecodeHalf(reinterpret_cast<const uint16_t*>(row)[x * 4 + channel]);

        case RHIFormat::RGBA32Float:
            return reinterpret_cast<const float*>(row)[x * 4 + channel];

        case RHIFormat::R32Float:
        case RHIFormat::D32Float:
            return (0 == channel) ? reinterpret_cast<const float*>(row)[x] : 0.f;

        case RHIFormat::R16Float:
            return (0 == channel) ? DecodeHalf(reinterpret_cast<const uint16_t*>(row)[x]) : 0.f;

        // SSAO의 AO 포맷이다(값 + 깊이 둘). R2c-b1이 목록을 만들 때 빠져
        // 있었고, R2c-b2에서 그 검사를 옮기다 "리드백이 모르는 포맷이다"로
        // 걸렸다 — 모르는 포맷을 0으로 넘기지 않고 실패로 만든 것이 값을 했다.
        case RHIFormat::RG16Float:
            return (channel < 2)
                ? DecodeHalf(reinterpret_cast<const uint16_t*>(row)[x * 2 + channel]) : 0.f;

        case RHIFormat::RGBA8Unorm:
        case RHIFormat::RGBA8UnormSrgb:
            return row[x * 4 + channel] / 255.f;

        case RHIFormat::R32Uint:
            return (0 == channel)
                ? static_cast<float>(reinterpret_cast<const uint32_t*>(row)[x]) : 0.f;

        default:
            // 모르는 포맷을 0으로 넘기면 '전부 검다'로 보여 원인이 멀다.
            // 여기 오면 위 목록에 더할 것.
            return 0.f;
        }
    }

    /// 버퍼 리드백을 원소 배열로 읽는다(R2c-b2).
    ///
    /// ★ 버퍼에는 포맷이 없어 At()가 답할 수 없다 — 그쪽은 모르는 포맷에
    ///   0을 주므로, 버퍼를 At()로 읽으면 '전부 0'이 조용히 나온다.
    ///   읽는 길을 따로 두어 그 혼동을 타입으로 막는다.
    template <typename T>
    const T* Elements() const
    {
        return (data.size() < sizeof(T)) ? nullptr
            : reinterpret_cast<const T*>(data.data());
    }

    template <typename T>
    size_t ElementCount() const { return data.size() / sizeof(T); }

    /// 반정밀도 → 단정밀도. 검사 열여섯 곳이 각자 갖고 있던 그 함수다.
    static float DecodeHalf(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1Fu;
        uint32_t mantissa = bits & 0x3FFu;

        if (0 == exponent)
        {
            if (0 == mantissa)
            {
                const uint32_t zero = sign;
                float out{}; std::memcpy(&out, &zero, sizeof(out)); return out;
            }
            // 비정규값 — 정규화될 때까지 지수를 내리며 가수를 민다.
            exponent = 1;
            while (0 == (mantissa & 0x400u)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FFu;
            const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
            float out{}; std::memcpy(&out, &bitsOut, sizeof(out)); return out;
        }
        if (31 == exponent)
        {
            const uint32_t bitsOut = sign | 0x7F800000u | (mantissa << 13);
            float out{}; std::memcpy(&out, &bitsOut, sizeof(out)); return out;
        }

        const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float out{}; std::memcpy(&out, &bitsOut, sizeof(out)); return out;
    }
};

/// 리드백 대상. 만든 시점과 읽는 시점 사이에 제출·대기가 끼므로 프레임을 넘어 산다.
struct RHIReadback
{
    /// ★ 포인터에서 핸들로 (5a). 여기가 이 파일에서 **유일하게 중립이 아니던
    ///   자리**였다(`ComPtr<ID3D12Resource>`), 그리고 그 한 필드 때문에
    ///   인코더의 리드백 넷이 Vulkan 이 구현할 수 없는 서명이었다 —
    ///   `CopyToReadback(const RHIReadback&, ...)` 이 이 구조체를 값으로 받는다.
    ///
    ///   대상 버퍼는 `CreateReadback` 이 만들어 표에 넣는다. 놓는 것은
    ///   `ReleaseReadback` 이다 — 리드백은 프레임을 넘어 살고 소유자가
    ///   호출부이므로, 표가 펜스를 안 보는 규약(V2-c1)이 그대로 적용된다.
    RHIBufferHandle buffer;

    uint32_t    width{ 0 };
    uint32_t    height{ 0 };

    /// 정렬된 행 간격. D3D12_TEXTURE_DATA_PITCH_ALIGNMENT를 호출부가 계산하던
    /// 자리이고, 18곳이 각자 상수로 들고 있었다.
    uint32_t    rowPitch{ 0 };
    RHIFormat   format{ RHIFormat::Unknown };

    uint32_t sliceCount{ 1 };
    size_t   sliceBytes{ 0 };

    bool IsValid() const { return buffer.IsValid(); }
};

/// 업로드된 메시의 바인딩 (A-4).
///
/// ★ `DX12MeshEntry` 를 대신한다. 저것은 `D3D12_VERTEX_BUFFER_VIEW` 와
///   `D3D12_INDEX_BUFFER_VIEW` 를 들었고, 그래서 인코더에 **원시 뷰를 받는
///   오버로드 둘**이 있어야 했다 — A-6 이 인코더의 마지막 DX12 심볼 둘로 세어
///   두고 "이것은 인코더의 문제가 아니라 자산 캐시에 딸린 것"이라고 적은 자리다.
///
/// ★ A-5a 의 `RHIBufferSlice` 를 그대로 쓴다. 링 조각만 뜻하는 타입이 아니라고
///   그때 적어 둔 것이 여기서 값을 한다 — 정점·인덱스 버퍼는 캐시가 소유한
///   것이고, 슬라이스는 그것을 통째로 가리킨다(`Whole`).
///
/// 정점 뷰가 든 세 값 중 크기는 슬라이스가, 보폭과 인덱스 포맷은 여기가 든다.
/// 보폭은 정점 레이아웃의 성질이고 포맷은 인덱스 폭이라, 둘 다 백엔드가 아니라
/// 메시가 아는 것이다.
struct RHIMeshBinding
{
    RHIBufferSlice vertices;
    uint32_t       vertexStride{ 0 };

    RHIBufferSlice indices;
    RHIFormat      indexFormat{ RHIFormat::R32Uint };
    uint32_t       indexCount{ 0 };

    bool IsValid() const { return 0 != indexCount; }
};

#endif
