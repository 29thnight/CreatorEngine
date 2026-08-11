#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <string>
#include <vector>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>

#include "DX12ResourceEntries.h"
#include "../RHIResourceTypes.h"   // 5a — 중립 값 타입이 여기서 갈렸다
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

/// A-3. 아래 `GetImmediateEncoder()` 가 돌려준다. 정의는 RHIEncoder.h 에 있고
/// 그쪽이 이 헤더를 물므로(테이블·슬라이스·리드백을 쓴다) 여기서는 이름만 안다.
class RHIEncoder;

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

// ★ 위 구조체들이 여기 있었다. 5a 에서 `RHI/RHIResourceTypes.h` 로 갈렸다 —
//   전부 이미 중립이었는데 이 헤더가 `d3d12.h` 를 물어서 Vulkan 이 못 쓰고
//   있었다. 이 파일이 그것을 include 하므로 소비처는 한 줄도 안 바뀐다.


/// 디바이스와 프레임 링. 지금 접점의 대부분(136/149)이 여기 셋에 몰려 있고,
/// 그것이 곧 R2·R3에서 없앨 대상이다.
class IRenderDeviceServices
{
public:
    virtual ~IRenderDeviceServices() = default;

    virtual ID3D12Device* GetDevice() const = 0;
    virtual ID3D12GraphicsCommandList* GetCommandList() const = 0;

    /// 프레임 링에서 자른다 (A-5a). 정렬은 용도가 정한다 — 상수는 256,
    /// 텍스처 복사원은 512, 구조화 버퍼는 원소 크기.
    ///
    /// ★ `GetUploadRing()` 을 대신한다. 저쪽은 구현 클래스 참조를 돌려주고
    ///   그 `Allocation` 이 `D3D12_GPU_VIRTUAL_ADDRESS` 를 들어서, R1 이
    ///   FrameContext 에서 걷어낸 "패스가 백엔드 구현을 안다"가 인터페이스
    ///   게터로 되살아나 있었다(§8.3 ①).
    virtual RHIBufferSlice AllocateUpload(uint64_t bytes, uint64_t alignment) = 0;

    /// 상수 하나를 올린다 — 자르고 복사하는 두 줄이 가장 흔한 형태라 접었다.
    /// 실패하면 무효 슬라이스다(링 구간이 찼다).
    virtual RHIBufferSlice UploadConstants(const void* data, size_t bytes) = 0;

    virtual DX12UploadRing& GetUploadRing() = 0;
    virtual DX12DescriptorRing& GetDescriptorRing() = 0;

    /// 샘플러 N개를 연속 테이블로 만든다 (A-4).
    ///
    /// ★ `GetSamplerHeap()` 을 대신한다. 저쪽은 구현 클래스 참조를 돌려주었고,
    ///   패스 셋이 거기서 `.ptr` 을 손으로 뽑아 테이블을 지었다 — A-5b 가
    ///   `RHISamplerTable` 을 불투명 값으로 만들었는데 **만드는 쪽**이 그대로
    ///   백엔드를 알던 자리다. `GetUploadRing()` 이 A-5a 에서 닫힌 것과 같은
    ///   부류의 마지막 하나이고, `CreateBindings` 가 SRV/UAV 에 하는 일을
    ///   샘플러에 한다.
    ///
    /// 실패하면 무효 테이블이다(힙 상한 — DX12 에서 2048).
    ///
    /// ★ 중복 제거를 계약에 넣지 않는다. 구현이 하나짜리 요청을 캐시로
    ///   흘리는 것은 관측 가능한 차이가 없는 최적화다(같은 설정이면 같은
    ///   디스크립터). 계약에 넣으면 호출부가 "하나씩 부르면 캐시된다"에
    ///   기대게 되고, 그러면 인접을 기대하는 옛 실수가 돌아온다.
    virtual RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc> descs) = 0;

    /// 지금 열린 커맨드 리스트에 기록하는 인코더 (A-3).
    ///
    /// ★ **`GetCommandList()` 와 다른 부류다.** 저것은 Vulkan 이 낼 수 없다 —
    ///   커맨드 버퍼는 풀에서 나고 타입도 기록 모델도 다르다. 이것은 낼 수
    ///   있다. 그래프 밖에서 커맨드를 적는 자리들이 원시 리스트를 들던 이유는
    ///   "인코더는 그래프가 만든다" 하나뿐이었는데, 인코더가 드는 것은
    ///   {커맨드 리스트, 리소스} 뿐이라 그래프 밖에서도 성립한다.
    ///
    /// ★ **그래프 안에서는 쓰지 말 것** — 거기서는 `executeContext.encoder` 를
    ///   쓴다. 조각마다 커맨드 리스트가 다르므로(AddSplitPass) 이것을 쓰면
    ///   워커가 서로의 기록을 덮는다.
    ///
    /// 돌려주는 참조는 프레임 동안 유효하다. `BeginFrame` 이 커맨드 리스트를
    /// 되감을 때 인코더도 다시 만든다 — 인코더가 기억하는 것(디스크립터 힙
    /// 바인딩)이 Reset 으로 낡기 때문이다.
    virtual RHIEncoder& GetImmediateEncoder() = 0;

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

    // ★ ClearUnorderedAccess 가 여기 있었다. A-3 에서 인터페이스를 떠났다.
    //
    //   R3-1 이 인코더에만 두었다가 "실제 호출부가 그래프 밖(VolumetricFog::
    //   PrepareFrame)이라 ExecuteContext 도 인코더도 없다"며 여기로 옮겼던
    //   자리다. 그 진단은 맞았고 처방이 반쪽이었다 — 없던 것은 **인코더**가
    //   아니라 **그래프 밖에서 인코더를 얻는 길**이었다. 호출부는 커맨드
    //   리스트를 꺼내(GetCommandList) 받는 쪽이 이미 아는 값을 인자로 도로
    //   넘기고 있었다.
    //
    //   GetImmediateEncoder() 가 그 길이고, 그러자 이 메서드의 존재 이유가
    //   사라졌다. 구현은 DX12DeviceResources 에 남는다(디스크립터 두 벌을
    //   맞추는 일이 힙 곁에 있어야 한다) — 인코더가 그리로 흘린다.

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
    /// 리소스가 스스로 아는 것 (5c-1). 핸들만 들고 있을 때 되묻는 길이다.
    ///
    /// ★ 이것이 없어서 패스 셋이 `Resolve(h)->GetDesc()` 로 포인터를 풀고
    ///   있었다 — 아래 `Resolve` 가 "과도기의 표시"라고 적어 둔 그 소비처의
    ///   일부다. 양쪽 백엔드가 똑같이 답할 수 있는 질문이라 계약에 둔다.
    virtual RHITextureInfo DescribeTexture(RHITextureHandle handle) const = 0;

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

    /// 리드백 대상을 놓는다 (5a).
    ///
    /// ★ `RHIReadback::buffer` 가 핸들이 되면서 소유가 표로 갔다. 예전에는
    ///   `ComPtr` 이 구조체와 함께 죽었는데, 이제는 놓으라고 해야 죽는다.
    ///
    /// ★ **지금 호출자는 0 이다.** 리드백을 만드는 34곳이 전부 자가 검증이고,
    ///   하네스가 검사당 프로세스를 하나씩 쓰므로(§7.2.7) 수명이 프로세스로
    ///   묶여 있다 — 표가 무한히 자랄 경로가 없다. 세어 둔다: 리드백을
    ///   프레임마다 만드는 코드가 생기면 그때는 반드시 불러야 하고,
    ///   V2-c1 이 그래프에서 겪은 것이 정확히 그 상황이다.
    virtual void ReleaseReadback(RHIReadback& readback) = 0;

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

// ★ `RHIMeshBinding` 이 여기 있었다. 5a 에서 `RHI/RHIResourceTypes.h` 로
//   갈렸다 — 중립인데 DX12 헤더에 있어서 Vulkan 메시 캐시가 못 쓴다.

/// 메시 업로드. 같은 메시를 여러 패스·여러 프레임이 공유한다.
class IRenderMeshCache
{
public:
    virtual ~IRenderMeshCache() = default;

    virtual RHIMeshBinding GetOrUpload(Mesh* mesh, std::string& outError) = 0;
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
