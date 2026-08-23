#pragma once
#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

#include "../RHIResourceTypes.h"

// 캐시가 돌려주는 값 타입들 (PHASE 3-1 재정의, R1).
//
// 원래 각 캐시 클래스의 중첩 타입(DX12MeshCache::Entry 등)이었다. 밖으로 뺀
// 이유는 순환이다 — 캐시를 인터페이스 뒤로 넣으려면 인터페이스가 Entry를
// 알아야 하고, Entry가 캐시 안에 있으면 인터페이스가 구현 헤더를 include해야
// 하며, 그 구현이 다시 인터페이스를 상속한다. Scene.h 순환을 끊을 때
// Entity::Index를 독립 헤더로 뺀 것과 같은 수법이다.
//
// 기존 이름(DX12MeshCache::Entry)은 별칭으로 남겨 호출부를 건드리지 않는다.
//
// ★ 아직 DX12 타입을 노출한다. R2(바인딩·뷰 이관)·R3(인코더)에서 이 값들이
//   RHI 핸들로 바뀌면 이 헤더도 함께 중립화되어 RHI/로 올라간다
//   (RhiBoundaryPlan.md §4).

// ★ `DX12MeshEntry` 가 여기 있었다. A-4 에서 `RHIMeshBinding`
//   (RenderFrameServices.h)으로 갈렸다 — 정점·인덱스 뷰 둘을 들고 있어서
//   인코더에 원시 뷰를 받는 오버로드 둘을 살려 두던 타입이다.
//
//   위 머리말이 "R2·R3 에서 이 값들이 RHI 핸들로 바뀌면 이 헤더도 함께
//   중립화되어 RHI/로 올라간다"고 적어 둔 그 일이고, 셋 중 하나가 갔다.
//   남은 둘은 텍스처 캐시의 `DXGI_FORMAT`(A-4b)과 파이프라인 표(백엔드
//   내부라 올라갈 이유가 없다)다.

/// 업로드된 텍스처. 큐브맵 여부와 배열 크기를 함께 싣는 이유는 소비자가
/// 그것을 모르면 큐브맵을 2D로 볼 수밖에 없기 때문이다(스카이박스 운반에서 추가).
// 소스 호환 별칭. 실체는 RHI/의 중립 값 타입이고 DX12 전용 포맷을 내지 않는다.
using DX12TextureEntry = RHITextureEntry;

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

