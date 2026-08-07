#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

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
    ID3D12Resource* resource{ nullptr };
    DXGI_FORMAT     format{ DXGI_FORMAT_UNKNOWN };
    uint32_t        width{ 0 };
    uint32_t        height{ 0 };
    uint32_t        mipLevels{ 0 };
    uint32_t        arraySize{ 1 };
    bool            isCube{ false };

    bool IsValid() const { return nullptr != resource; }
};

/// 캐시된 루트 시그니처. id는 그대로 DX12GraphicsPipelineDesc::rootSignatureId에 넣는다.
struct DX12RootSignatureEntry
{
    ID3D12RootSignature* signature{ nullptr };
    uint64_t             id{ 0 };

    bool IsValid() const { return nullptr != signature; }
};

#endif
