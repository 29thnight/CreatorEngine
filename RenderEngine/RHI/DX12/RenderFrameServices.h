#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <string>
#include <vector>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>

#include "DX12ResourceEntries.h"
#include "../RHIFormat.h"
#include "DX12Format.h"
#include "../RHIHandle.h"
#include "../RHIResourceState.h"
#include "../RHIPipelineLayout.h"
#include "../RHIPipelineState.h"
#include "../IRenderPipelineCache.h"

class Mesh;
class Texture;
class DX12UploadRing;
class DX12DescriptorRing;
class DX12SamplerHeap;

// 패스가 프레임 동안 쓰는 백엔드 서비스 (PHASE 3-1 재정의, R1).
//
// ── 왜 인터페이스인가 ──
//
// 예전에는 EnhancedFrameContext가 구현 클래스 다섯(DX12DeviceResources ·
// DX12PSOManager · DX12RootSignatureCache · DX12MeshCache · DX12TextureCache)을
// 그대로 노출했고, 패스가 DX12에 닿는 모든 경로가 그 구조체를 지났다.
// 여기가 최상위 절단선이라 첫 슬라이스의 대상이 됐다.
//
// ── 접점이 얼마나 좁은지 세어 보고 정했다 ──
//
// 패스 17종 12,523줄에서 이 다섯을 거치는 호출은 열두 종류뿐이다:
//
//   resources      GetDevice 49 · GetDescriptorRing 49 · GetUploadRing 38 ·
//                  GetSamplerHeap 6 · GetCommandList 2
//   psoManager     GetOrCreate 16 · GetOrCreateCompute 10
//   rootSignatures GetOrCreate 20
//   meshCache      GetOrUpload 4
//   textureCache   GetOrUpload 5 · GetOrmNeutralTexture 2 · GetBlackTexture 2
//
// 그래서 인터페이스가 이 열둘로 끝난다. 메서드 이름을 구현과 같게 둔 것도
// 의도다 — 패스 본문 149곳을 한 줄도 고치지 않고 타입만 갈아 끼운다.
//
// ── 아직 DX12 타입을 노출한다 ──
//
// ID3D12Device* · DX12DescriptorRing& 같은 것이 그대로 서명에 있다. 이 단계의
// 목적은 "패스가 백엔드 *구현 클래스*를 아는 상태"를 끝내는 것이지 d3d12.h
// 의존을 끊는 것이 아니다. 그 의존은 R2(뷰 생성·디스크립터 바인딩을
// CreateBindings 한 줄로)와 R3(인코더)에서 메서드가 사라지며 함께 사라진다 —
// 위 빈도표의 136/149건이 그 둘의 사정권이다.
//
// 다시 말해 이 헤더는 오래 살 것이 아니다. R2·R3가 끝나면 남는 것은 자산
// 캐시 둘 정도이고, 그때 RHI/로 올라가며 이름과 타입이 중립화된다
// (RhiBoundaryPlan.md §3.1·§4).

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

/// 잘라 둔 연속 테이블. 수명은 이 프레임이다.
///
/// ★ 아직 D3D12 GPU 핸들을 그대로 들고 있다. 패스가 이것을
///   SetGraphicsRootDescriptorTable에 넘기기 때문이고, 그 호출이 R3에서
///   인코더로 옮겨 가면 이 타입도 불투명 핸들이 된다.
struct RHIBindingTable
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    uint32_t                    count{ 0 };

    bool IsValid() const { return 0 != count; }
};

/// 샘플러 테이블 (R3-2).
///
/// ★ 같은 D3D12_GPU_DESCRIPTOR_HANDLE인데 왜 RHIBindingTable을 안 쓰는가:
///   샘플러는 **다른 힙**에서 온다. SRV 테이블 핸들을 샘플러 슬롯에 걸면
///   컴파일도 되고 검증 레이어도 대개 지나가지만 GPU에서 잘못 읽는다.
///   타입을 갈라 두면 그 실수가 표현 불가능해진다 — R2b의
///   RHIRenderTargetBinding이 핸들 대신 인덱스를 든 것과 같은 이유다.
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
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};

    bool IsValid() const { return 0 != gpu.ptr; }
};

// ── 렌더 타깃 (R2b) ──
//
// R2a가 셰이더 가시 테이블(SRV/UAV)을 걷었고, 여기가 그 나머지다. 성격이
// 달라 인터페이스도 다르다: RTV/DSV는 디스크립터 링이 아니라 **패스마다 자기
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

/// 만들어 둔 RTV/DSV 묶음. 수명은 이 프레임이다.
///
/// ★ RHIBindingTable과 달리 핸들이 아니라 인덱스를 든다. 밖에서 핸들 산술을
///   할 길을 두지 않기 위해서다 — 패스가 GetDescriptorHandleIncrementSize를
///   부르던 6곳이 정확히 그 산술이었다.
struct RHIRenderTargetBinding
{
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t rtvIndex{ kInvalidIndex };
    uint32_t colorCount{ 0 };
    uint32_t dsvIndex{ kInvalidIndex };

    bool HasColor() const { return 0 != colorCount; }
    bool HasDepth() const { return kInvalidIndex != dsvIndex; }
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
// ★ 초기 상태는 아직 D3D12_RESOURCE_STATES다. RHIResourceState가 이미 백엔드
//   중립이라 그쪽이 옳지만, 그 타입은 그래프 헤더에 있고 경계 헤더가 그래프를
//   끌어오는 것은 방향이 거꾸로다. 계획대로 R4에서 RHIResourceState를 RHI로
//   올릴 때 이 필드도 함께 중립화한다(RhiBoundaryPlan §3.3).

struct RHIBufferDesc
{
    uint64_t bytes{ 0 };
    bool     allowUnorderedAccess{ false };

    /// ★ 버퍼는 COMMON으로 만들고 첫 사용에서 승격시키는 것이 맞다.
    ///   UNORDERED_ACCESS를 초기 상태로 주면 검증 레이어가 '무시한다'고
    ///   경고만 남기고 지나간다 — 틀렸는데 조용한 부류라 기본값으로 박아 둔다.
    D3D12_RESOURCE_STATES initialState{ D3D12_RESOURCE_STATE_COMMON };

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

    D3D12_RESOURCE_STATES initialState{ D3D12_RESOURCE_STATE_COMMON };
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
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

    uint32_t    width{ 0 };
    uint32_t    height{ 0 };

    /// 정렬된 행 간격. D3D12_TEXTURE_DATA_PITCH_ALIGNMENT를 호출부가 계산하던
    /// 자리이고, 18곳이 각자 상수로 들고 있었다.
    uint32_t    rowPitch{ 0 };
    RHIFormat   format{ RHIFormat::Unknown };

    uint32_t sliceCount{ 1 };
    size_t   sliceBytes{ 0 };

    bool IsValid() const { return nullptr != buffer.Get(); }
};

/// 디바이스와 프레임 링. 지금 접점의 대부분(136/149)이 여기 셋에 몰려 있고,
/// 그것이 곧 R2·R3에서 없앨 대상이다.
class IRenderDeviceServices
{
public:
    virtual ~IRenderDeviceServices() = default;

    virtual ID3D12Device* GetDevice() const = 0;
    virtual ID3D12GraphicsCommandList* GetCommandList() const = 0;

    virtual DX12UploadRing& GetUploadRing() = 0;
    virtual DX12DescriptorRing& GetDescriptorRing() = 0;
    virtual DX12SamplerHeap& GetSamplerHeap() = 0;

    // ── R2에서 더한 것 ──

    /// 뷰 여럿을 연속 테이블로 잘라 한 번에 만든다.
    ///
    /// 링이 모자라거나 리소스가 널이면 invalid를 돌려준다 — 호출부는 그것
    /// 하나만 검사하면 된다. 예전에는 Allocate 검사와 뷰별 널 검사가 따로
    /// 흩어져 있었고, 뒤엣것은 대개 빠져 있었다.
    virtual RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descs) = 0;

    // ★ BindDescriptorHeaps도 R4-1b에서 인코더로 갔다(R2b의 거는 셋과 같은 수).
    //
    //   구현은 DX12DeviceResources에 남는다. 그래프 밖에서 부르는 자리 둘
    //   (인코더 벤치의 원시 경로 대조 · SSGI 자가 검증의 셋업 디스패치)이
    //   그 구체 타입을 들고 있어 그대로 돈다 — 이중 거처가 아니라, 인터페이스가
    //   좁아지고 구현이 제자리에 있는 것이다.
    //
    //   ★ 아직 끝이 아니다. 이 인터페이스에 ID3D12GraphicsCommandList가 셋
    //     남아 있다: GetCommandList · ClearUnorderedAccess · CopyToReadback.
    //     앞의 둘은 그래프 밖 호출부(VolumetricFog::PrepareFrame 따위)가 있어
    //     인코더만으로는 못 덮고, CopyToReadback은 자가 검증이 쓰는 자리라
    //     R6(가짜 백엔드로 하네스 대체)의 몫이다. 세어 둔다 — 남은 수를
    //     적어 두지 않으면 다음 슬라이스가 다 끝났다고 착각한다.

    // ── R2b에서 더한 것 ──

    /// 색 타깃 N개와 (있으면) 깊이 타깃의 뷰를 프레임 힙에 만든다.
    ///
    /// 색 리소스가 하나라도 널이거나 힙이 모자라면 invalid를 돌려준다 —
    /// CreateBindings와 같은 계약이라 호출부는 그것 하나만 검사한다.
    ///
    /// depth가 nullptr이면 색만 만든다(그림자처럼 깊이만 쓰는 패스는 colors를
    /// 비우고 depth만 준다).
    virtual RHIRenderTargetBinding CreateRenderTargets(
        std::span<const RHITextureHandle> colors,
        const RHIDepthTargetDesc* depth = nullptr) = 0;

    // ★ 거는 셋(BindRenderTargets · ClearRenderTargets · ClearDepthTarget)은
    //   R4-1에서 인코더로 갔다. 만드는 것과 거는 것을 가른 것인데, 그 경계는
    //   '커맨드를 적는가'다 — 위의 CreateRenderTargets는 뷰를 힙에 만들 뿐이고
    //   커맨드 리스트를 보지 않는다.
    //
    //   구현은 DX12DeviceResources에 그대로 있고 DX12Encoder가 그리로 흘린다.
    //   달라진 것은 이 **인터페이스**가 커맨드 리스트를 더 이상 말하지 않는
    //   다는 것이다. 그래야 패스가 서비스에 넘기려고 커맨드 리스트를 들고
    //   있을 이유가 사라진다.

    /// UAV를 값 하나로 채운다 (R3-2).
    ///
    /// ★ 인코더에 같은 이름이 있는데 여기에도 두는 이유: 실제 호출부가
    ///   그래프 밖이다. VolumetricFog는 격자를 첫 프레임에 한 번 지우는데
    ///   그 자리가 PrepareFrame이라 ExecuteContext도 인코더도 없다.
    ///
    ///   R3-1이 인코더에만 두면서 "이 하나가 VolumetricFog의 두 번째 힙을
    ///   없앤다"고 적었는데, 호출부의 위치를 안 보고 적은 것이었다 —
    ///   그 메서드는 호출자가 하나도 없었다. 없애 주는 것은 맞고, 통로가
    ///   인코더가 아니라 여기다.
    ///
    ///   DX12의 ClearUnorderedAccessViewFloat가 같은 UAV의 셰이더 가시 GPU
    ///   핸들과 비가시 CPU 핸들을 짝으로 요구하는 것을 안에서 맞춘다.
    ///   호출부는 "이 리소스를 이 값으로 지운다"만 적는다.
    virtual void ClearUnorderedAccess(ID3D12GraphicsCommandList* commandList,
        const RHIBindingDesc& view, const float rgba[4]) = 0;

    // ── R2c에서 더한 것 ──
    //
    // 프레임을 넘어 사는 패스 소유 리소스. 실패하면 false를 돌려주고
    // outError에 이유를 담는다 — 호출부가 HRESULT를 문자열로 바꾸던 코드가
    // 세 곳에 흩어져 있었고 형식도 제각각이었다.

    /// 핸들 → 실제 리소스. 백엔드만 답할 수 있는 질문이다.
    ///
    /// ★ 이 함수가 인터페이스에 있는 것이 과도기의 표시다. 지금은 상위가
    ///   핸들을 받아 다시 포인터로 풀어 쓰는 자리가 남아 있고(뷰 설명·
    ///   임포트·루트 직결 주소), V2-b·V2-c 가 그 자리들을 핸들째 받게 되면
    ///   호출부가 사라진다. 그때 이 둘도 백엔드 내부로 내려간다.
    virtual ID3D12Resource* Resolve(RHITextureHandle handle) const = 0;
    virtual ID3D12Resource* Resolve(RHIBufferHandle handle) const = 0;

    /// 핸들 → 파이프라인과 그 루트 시그니처 (A-1).
    ///
    /// ★ **여기 두고 싶지 않았다.** A-1 의 설계는 "표는 구현 클래스에 두고
    ///   인터페이스에는 안 올린다"였다 — 인코더가 DX12DeviceResources 를 이미
    ///   들고 있으므로 그것으로 충분할 줄 알았다(§7.2.6 ③·예상 6).
    ///
    ///   틀렸다. 소비자가 하나 더 있다: `EnhancedIBLGenerator` 가 그래프 밖에서
    ///   **원시 커맨드 리스트**에 직접 파이프라인을 건다(`GetCommandList()` ·
    ///   `SetPipelineState`). 인코더를 안 타므로 인코더의 해소를 못 쓴다.
    ///
    ///   그래서 위의 `Resolve(RHITextureHandle)` 과 **같은 부류**로 둔다 —
    ///   그 주석이 "이 함수가 인터페이스에 있는 것이 과도기의 표시다"라고
    ///   적어 둔 그것이다. 원시 커맨드 리스트를 쓰는 자리가 없어지면(A-3)
    ///   이 셋이 함께 내려간다.
    virtual DX12PipelineEntry Resolve(RHIPipelineHandle handle) const = 0;

    /// 소유하지 않고 표에 올린다 — 이미 ComPtr을 든 쪽이 핸들도 필요할 때.
    ///
    /// ★ 이쪽은 과도기가 아니다. IBL 생성기·텍스처 캐시처럼 리소스를 스스로
    ///   만들어 오래 들고 있는 것들이 소비처(desc)에 핸들을 주려면 필요하다.
    ///   놓는 것은 등록한 쪽의 책임이다 — 표는 펜스를 보지 않는다.
    virtual RHITextureHandle RegisterExternalTexture(ID3D12Resource* resource) = 0;
    virtual void ReleaseTexture(RHITextureHandle handle) = 0;

    /// 그래프 밖에서 상태를 바꾼다. 지금 열려 있는 커맨드 리스트에 기록한다.
    ///
    /// ★ 그래프 안에서는 부르면 안 된다 — 거기서는 usage 선언이 배리어를
    ///   만들고, 손으로 하나 더 걸면 그래프가 아는 상태와 실제가 어긋난다.
    ///   R3가 "상태 전이는 그래프의 몫"이라고 정한 계약은 그대로다. 이것은
    ///   그 계약 **밖**의 자리들을 위한 것이다: 업로드 직후의 한 번짜리 전이,
    ///   캐시가 올린 텍스처를 넓히는 자리, 자가 검증이 리드백 전에 거는 전이.
    ///
    ///   그 자리들이 지금까지 D3D12_RESOURCE_BARRIER를 손으로 채우고 있었다.
    ///   Vulkan에서는 그것이 VkImageMemoryBarrier + 레이아웃 + 접근 마스크 +
    ///   스테이지가 되므로, 원시 구조체를 상위가 채우면 옮길 수 없다.
    virtual void TransitionResources(std::span<const RHITransition> transitions) = 0;

    /// ★ 핸들을 돌려준다(V2-a). 만든 리소스는 표가 들고, 호출부는 핸들만
    ///   남긴다 — 소유는 표로 옮겨가지만 수명 규약은 그대로다(Shutdown 까지).
    virtual bool CreateBuffer(const RHIBufferDesc& desc,
        RHIBufferHandle& outHandle, std::string& outError) = 0;

    virtual bool CreateTexture(const RHITextureDesc& desc,
        RHITextureHandle& outHandle, std::string& outError) = 0;

    // ── 리드백 (R2c-b) ──

    /// 리드백 대상을 만든다. 행 간격 정렬은 여기서 한 번만 한다.
    ///
    /// sliceCount가 1보다 크면 같은 크기의 장을 연달아 담는다 — 데칼처럼
    /// 확산·노멀·ORM 셋을 한 번에 뜨는 검사가 그것을 쓴다.
    virtual bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
        uint32_t sliceCount, RHIReadback& outReadback, std::string& outError) = 0;

    /// 기록 시점에 복사를 넣는다. 원본은 COPY_SOURCE 상태여야 한다
    /// (그래프가 선언으로 그 상태를 만들어 준다).
    virtual void CopyToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) = 0;

    /// 3D 텍스처를 통째로 뜬다. 깊이 한 켜가 장 하나다.
    ///
    /// ★ 장 모델이 그대로 맞는다 — D3D12의 배치 풋프린트는 3D도 행 간격
    ///   하나로 z·y를 이어 놓으므로, sliceBytes = rowPitch × height이고
    ///   z가 곧 장 번호다. 그래서 읽는 쪽은 At(x, y, 채널, z)로 끝난다.
    ///   리드백의 sliceCount가 볼륨 깊이와 같아야 한다.
    virtual void CopyVolumeToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t sourceSubresource = 0) = 0;

    /// 원본의 왼쪽 위 모서리만 뜬다 — 뜨는 크기는 리드백 자신의 것이다.
    ///
    /// ★ 실제 쓰임 셋이 전부 같은 모양이었다(SSAO 스케일 · 포스트 스케일 ·
    ///   라이브의 post_probe): "결과를 살려 두려고 8x1만 옮긴다". 그래프가
    ///   소비자 없는 패스를 걷어내므로 소비자가 필요한데, 전부 옮기면 복사
    ///   시간이 계측에 섞인다. 그래서 크기를 따로 받지 않는다 — 리드백을
    ///   8x1로 만든 것이 곧 "8x1만 뜬다"는 뜻이다.
    virtual void CopyPartialToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) = 0;

    /// 제출·대기가 끝난 뒤 값을 읽는다. Map·복사·Unmap을 한 번에 한다 —
    /// 호출부가 Unmap을 빠뜨릴 자리를 없앤다.
    virtual bool MapReadback(const RHIReadback& readback,
        RHIReadbackImage& outImage, std::string& outError) = 0;

    // ── 버퍼 리드백 (R2c-b2) ──
    //
    // ★ 텍스처와 시그니처가 갈린다. R2c-b1이 "형태는 b2에서 실제 쓰임을 보고
    //   정하는 것이 맞다"며 남겨 둔 자리이고, 실제 쓰임은 셋이었다 —
    //   Forward+ 타일 카운트(uint32 배열) 둘과 타일 목록 하나. 전부
    //   "구조화 버퍼를 통째로 떠서 원소 배열로 읽는다"라 크기 하나면 된다.
    //
    //   포맷도 행 간격도 없다. 픽셀이 아니므로 정렬할 행이 없고, 그래서
    //   RHIReadbackImage의 At()가 아니라 Elements<T>()로 읽는다.

    /// 버퍼 리드백 대상. 크기는 바이트다.
    virtual bool CreateBufferReadback(uint64_t bytes,
        RHIReadback& outReadback, std::string& outError) = 0;

    /// 기록 시점에 버퍼 복사를 넣는다. bytes가 0이면 리드백 크기만큼 전부.
    /// 원본은 COPY_SOURCE 상태여야 한다(그래프가 선언으로 만들어 준다).
    virtual void CopyBufferToReadback(ID3D12GraphicsCommandList* commandList,
        const RHIReadback& readback, ID3D12Resource* source,
        uint64_t sourceOffset = 0, uint64_t bytes = 0) = 0;
};

// ★ IRenderPipelineCache · IRenderRootSignatureCache 가 여기 있었다.
//   A-1b 에서 RHI/IRenderPipelineCache.h 로 옮겼다 — 서명이 중립이 됐는데도
//   선언이 이 헤더(d3d12.h 를 문다)에 있어서 Vulkan 이 상속을 못 했다.
//   include 는 위에 있으므로 이 헤더를 쓰던 호출부는 한 줄도 안 바뀐다.

/// 메시 업로드. 같은 메시를 여러 패스·여러 프레임이 공유한다.
class IRenderMeshCache
{
public:
    virtual ~IRenderMeshCache() = default;

    virtual DX12MeshEntry GetOrUpload(Mesh* mesh, std::string& outError) = 0;
};

/// 텍스처 업로드와 폴백. 폴백 둘(검정·ORM 중립)이 인터페이스에 있는 이유는
/// 재질에 텍스처가 없을 때 패스가 그것을 직접 고르기 때문이다 — 널 디스크립터를
/// 테이블에 남기면 검증 레이어가 잡는다.
class IRenderTextureCache
{
public:
    virtual ~IRenderTextureCache() = default;

    virtual DX12TextureEntry GetOrUpload(Texture* texture, std::string& outError) = 0;
    virtual DX12TextureEntry GetBlackTexture(std::string& outError) = 0;
    virtual DX12TextureEntry GetOrmNeutralTexture(std::string& outError) = 0;
};

#endif
