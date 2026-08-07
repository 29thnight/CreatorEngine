#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <string>
#include <d3d12.h>

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

    /// UNKNOWN이면 리소스 포맷을 그대로 쓴다. 깊이를 읽을 때만 명시하면 된다.
    DXGI_FORMAT     format{ DXGI_FORMAT_UNKNOWN };

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
