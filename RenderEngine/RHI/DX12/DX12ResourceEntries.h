#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

#include "../RHIHandle.h"

// 캐시가 돌려주는 값 타입들 (PHASE 3-1 재정의, R1).
//
// 원래 각 캐시 클래스의 중첩 타입(DX12MeshCache::Entry 등)이었다. 밖으로 뺀
// 이유는 순환이다 — 캐시를 인터페이스 뒤로 넣으려면 인터페이스가 Entry를
// 알아야 하고, Entry가 캐시 안에 있으면 인터페이스가 구현 헤더를 include해야
// 하며, 그 구현이 다시 인터페이스를 상속한다. Scene.h 순환을 끊을 때
// GameObject::Index를 독립 헤더로 뺀 것과 같은 수법이다.
//
// 기존 이름(DX12MeshCache::Entry)은 별칭으로 남겨 호출부를 건드리지 않는다.
//
// ★ 아직 DX12 타입을 노출한다. R2(바인딩·뷰 이관)·R3(인코더)에서 이 값들이
//   RHI 핸들로 바뀌면 이 헤더도 함께 중립화되어 RHI/로 올라간다
//   (RhiBoundaryPlan.md §4).

/// 업로드된 메시의 바인딩 정보.
struct DX12MeshEntry
{
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW  indexView{};
    uint32_t                 indexCount{ 0 };

    bool IsValid() const { return 0 != indexCount; }
};

/// 업로드된 텍스처. 큐브맵 여부와 배열 크기를 함께 싣는 이유는 소비자가
/// 그것을 모르면 큐브맵을 2D로 볼 수밖에 없기 때문이다(스카이박스 운반에서 추가).
struct DX12TextureEntry
{
    // ★ 포인터에서 핸들로 (V2-b1). 머티리얼 텍스처는 desc 호출부의 큰
    //   덩어리인데(batch.textures[i] · IBL · 그림자 · 폴백) 캐시가 포인터를
    //   내는 한 그 자리들이 핸들을 받을 수 없다. 캐시가 표에 등록하는 것이
    //   V2-b의 전제다.
    //
    //   캐시는 소유를 넘기지 않는다 — ComPtr은 Resident가 계속 들고, 표에는
    //   빌려주기(external)로 올린다. 은퇴할 때 표에서도 놓으므로, 은퇴한
    //   텍스처의 핸들은 죽어 가는 리소스가 아니라 nullptr로 풀린다.
    RHITextureHandle handle;
    DXGI_FORMAT      format{ DXGI_FORMAT_UNKNOWN };
    uint32_t         width{ 0 };
    uint32_t         height{ 0 };
    uint32_t         mipLevels{ 0 };
    uint32_t         arraySize{ 1 };
    bool             isCube{ false };

    bool IsValid() const { return handle.IsValid(); }
};

// ── 파이프라인 표가 드는 것 (A-1) ──
//
// ★ `DX12RootSignatureEntry` 가 여기 있었다. 캐시가 `{signature, id}` 를 밖으로
//   내보내던 타입이고, 그래서 패스가 `ID3D12RootSignature*` 를 멤버로 들었다.
//   이제 캐시는 `RHIPipelineLayoutHandle` 을 주고 아래 둘은 **표 안쪽**에서만
//   쓰인다 — 밖으로 나가지 않는다.

/// 표 한 칸: 파이프라인과 그것이 구워진 레이아웃.
///
/// ★ 둘을 한 칸에 두는 것이 A-1 의 핵심이다. 따로 두면 "파이프라인 P 를
///   레이아웃 L' 로 걸었다"가 표현 가능해지고 Vulkan 에서 미정의가 된다
///   (RHIHandle.h ★). DX12 에서는 그 조합이 조용히 잘못된 루트 상태를 만든다.
struct DX12PipelineEntry
{
    ID3D12PipelineState* pipeline{ nullptr };
    ID3D12RootSignature* signature{ nullptr };

    bool IsValid() const { return nullptr != pipeline; }
};

/// 표 한 칸: 루트 시그니처와 그 **안정 해시**.
///
/// ★ 해시를 같이 드는 이유가 A-1 의 함정이다. 핸들은 슬롯+세대라 실행마다
///   달라지는데, PSO 디스크 캐시의 키는 실행을 넘어 안정해야 한다. 그래서
///   desc 는 핸들을 들고, 해시할 때 백엔드가 이 값으로 푼다
///   (`RHIPipelineState.h` 의 ★ 참고).
struct DX12PipelineLayoutEntry
{
    ID3D12RootSignature* signature{ nullptr };
    uint64_t             stableHash{ 0 };

    bool IsValid() const { return nullptr != signature; }
};

#endif
