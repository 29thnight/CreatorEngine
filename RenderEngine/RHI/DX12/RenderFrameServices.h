#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <string>
#include <d3d12.h>
#include <wrl/client.h>

#include "DX12ResourceEntries.h"

class Mesh;
class Texture;
class DX12UploadRing;
class DX12DescriptorRing;
class DX12SamplerHeap;
struct DX12GraphicsPipelineDesc;
struct DX12ComputePipelineDesc;

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

    Kind            kind{ Kind::ShaderResource };
    Dim             dim{ Dim::Default };
    ID3D12Resource* resource{ nullptr };

    /// UNKNOWN이면 리소스가 스스로 아는 포맷을 쓴다.
    DXGI_FORMAT     format{ DXGI_FORMAT_UNKNOWN };

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
    static RHIBindingDesc Srv(ID3D12Resource* resource)
    {
        RHIBindingDesc d{}; d.resource = resource; return d;
    }

    /// 포맷을 갈아서 본다. 깊이 버퍼를 셰이더로 읽는 자리가 이것이다.
    static RHIBindingDesc Srv2D(ID3D12Resource* resource, DXGI_FORMAT format,
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
    static RHIBindingDesc SrvDepth(ID3D12Resource* resource)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture2D; d.resource = resource; d.depthAsColor = true;
        return d;
    }

    static RHIBindingDesc SrvArray(ID3D12Resource* resource, DXGI_FORMAT format,
        uint32_t sliceCount, uint32_t firstSlice = 0)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture2DArray; d.resource = resource; d.format = format;
        d.firstSlice = firstSlice; d.sliceCount = sliceCount;
        return d;
    }

    static RHIBindingDesc SrvCube(ID3D12Resource* resource, DXGI_FORMAT format,
        uint32_t mipLevels = 1)
    {
        RHIBindingDesc d{};
        d.dim = Dim::TextureCube; d.resource = resource; d.format = format;
        d.mipLevels = mipLevels;
        return d;
    }

    static RHIBindingDesc Srv3D(ID3D12Resource* resource, DXGI_FORMAT format)
    {
        RHIBindingDesc d{};
        d.dim = Dim::Texture3D; d.resource = resource; d.format = format;
        return d;
    }

    static RHIBindingDesc Uav2D(ID3D12Resource* resource,
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, uint32_t mipSlice = 0)
    {
        RHIBindingDesc d{};
        d.kind = Kind::UnorderedAccess; d.dim = Dim::Texture2D;
        d.resource = resource; d.format = format; d.mostDetailedMip = mipSlice;
        return d;
    }

    static RHIBindingDesc Uav3D(ID3D12Resource* resource, DXGI_FORMAT format,
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

    static RHIBindingDesc UavBuffer(ID3D12Resource* resource, uint32_t numElements,
        uint32_t structureByteStride, uint32_t firstElement = 0)
    {
        RHIBindingDesc d{};
        d.kind = Kind::UnorderedAccess; d.dim = Dim::Buffer; d.resource = resource;
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
    ID3D12Resource* resource{ nullptr };

    /// 필수다. 깊이는 리소스 포맷 그대로 뷰를 만들 수 없는 경우가 있어
    /// (D32_FLOAT_S8X24_UINT 같은 것) 호출부가 무엇으로 볼지 정해야 한다.
    DXGI_FORMAT     format{ DXGI_FORMAT_UNKNOWN };

    /// 깊이를 셰이더로 읽으면서 동시에 묶을 때 켠다. 그래프가 DepthRead로
    /// 선언한 자리의 계약이고(EnhancedRenderGraph.h), 빠뜨리면 상태 전이와
    /// 어긋나 검증 레이어가 잡는다.
    bool            readOnly{ false };

    /// sliceCount가 0이면 Texture2D, 1 이상이면 Texture2DArray로 본다.
    /// 그림자 캐스케이드가 배열 한 장씩 묶는 자리다.
    uint32_t        firstSlice{ 0 };
    uint32_t        sliceCount{ 0 };

    static RHIDepthTargetDesc Depth(ID3D12Resource* resource, DXGI_FORMAT format)
    {
        RHIDepthTargetDesc d{}; d.resource = resource; d.format = format; return d;
    }

    static RHIDepthTargetDesc DepthReadOnly(ID3D12Resource* resource, DXGI_FORMAT format)
    {
        RHIDepthTargetDesc d{}; d.resource = resource; d.format = format;
        d.readOnly = true; return d;
    }

    static RHIDepthTargetDesc DepthSlice(ID3D12Resource* resource, DXGI_FORMAT format,
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
// ★ 초기 상태는 아직 D3D12_RESOURCE_STATES다. RGResourceState가 이미 백엔드
//   중립이라 그쪽이 옳지만, 그 타입은 그래프 헤더에 있고 경계 헤더가 그래프를
//   끌어오는 것은 방향이 거꾸로다. 계획대로 R4에서 RGResourceState를 RHI로
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
    DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };

    bool        allowUnorderedAccess{ false };
    bool        allowRenderTarget{ false };

    D3D12_RESOURCE_STATES initialState{ D3D12_RESOURCE_STATE_COMMON };
    const wchar_t* debugName{ nullptr };
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

    /// 이번 드로우가 쓸 디스크립터 힙을 건다. 샘플러 힙은 쓰는 패스만 켠다.
    ///
    /// 따로 두는 이유: 힙 바인딩을 빠뜨리면 테이블 핸들이 다른 힙을 가리켜
    /// 엉뚱한 리소스를 읽는다. 화면에는 '가끔 이상한 텍스처'로만 나타난다.
    virtual void BindDescriptorHeaps(ID3D12GraphicsCommandList* commandList,
        bool withSamplers = false) = 0;

    // ── R2b에서 더한 것 ──

    /// 색 타깃 N개와 (있으면) 깊이 타깃의 뷰를 프레임 힙에 만든다.
    ///
    /// 색 리소스가 하나라도 널이거나 힙이 모자라면 invalid를 돌려준다 —
    /// CreateBindings와 같은 계약이라 호출부는 그것 하나만 검사한다.
    ///
    /// depth가 nullptr이면 색만 만든다(그림자처럼 깊이만 쓰는 패스는 colors를
    /// 비우고 depth만 준다).
    virtual RHIRenderTargetBinding CreateRenderTargets(
        std::span<ID3D12Resource* const> colors,
        const RHIDepthTargetDesc* depth = nullptr) = 0;

    /// OMSetRenderTargets. 색이 없으면 깊이만 묶는다.
    virtual void BindRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding) = 0;

    /// 색 타깃 전부를 같은 값으로 지운다.
    ///
    /// ★ 타깃마다 다른 값을 받지 않는다. 지금 여덟 곳이 전부 같은 값이고
    ///   (대개 0), 낱개를 열어 두면 '어느 타깃이 몇 번인가'를 호출부가 다시
    ///   세게 된다 — R2b가 없애려는 바로 그 산술이다. 필요해지면 그때 넓힌다.
    virtual void ClearRenderTargets(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, const float rgba[4]) = 0;

    /// 깊이를 지운다. 깊이가 없는 바인딩이면 아무 일도 하지 않는다.
    virtual void ClearDepthTarget(ID3D12GraphicsCommandList* commandList,
        const RHIRenderTargetBinding& binding, float depth) = 0;

    // ── R2c에서 더한 것 ──
    //
    // 프레임을 넘어 사는 패스 소유 리소스. 실패하면 false를 돌려주고
    // outError에 이유를 담는다 — 호출부가 HRESULT를 문자열로 바꾸던 코드가
    // 세 곳에 흩어져 있었고 형식도 제각각이었다.

    virtual bool CreateBuffer(const RHIBufferDesc& desc,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outResource, std::string& outError) = 0;

    virtual bool CreateTexture(const RHITextureDesc& desc,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outResource, std::string& outError) = 0;
};

/// PSO 캐시. desc 해시로 파이프라인을 나눠 쓴다 — 뷰가 둘이어도 컴파일은 한 번이다.
class IRenderPipelineCache
{
public:
    virtual ~IRenderPipelineCache() = default;

    virtual ID3D12PipelineState* GetOrCreate(
        const DX12GraphicsPipelineDesc& desc, std::string& outError) = 0;
    virtual ID3D12PipelineState* GetOrCreateCompute(
        const DX12ComputePipelineDesc& desc, std::string& outError) = 0;
};

class IRenderRootSignatureCache
{
public:
    virtual ~IRenderRootSignatureCache() = default;

    virtual DX12RootSignatureEntry GetOrCreate(
        const D3D12_ROOT_SIGNATURE_DESC& desc, std::string& outError) = 0;
};

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
