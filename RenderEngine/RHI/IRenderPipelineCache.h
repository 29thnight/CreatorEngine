#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <string>

#include "RHIHandle.h"
#include "RHIPipelineLayout.h"
#include "RHIPipelineState.h"

// 파이프라인·레이아웃 캐시 인터페이스 — 백엔드 중립 (A-1b).
//
// ── 왜 여기로 옮겼는가 ──
//
// A-1a 가 이 둘의 **서명**을 중립으로 갈았다. 인자도 반환형도 DX12 가 아니다.
// 그런데 두 번째 백엔드는 여전히 상속할 수 없었다 — 선언이
// `RHI/DX12/RenderFrameServices.h` 에 있고 그 헤더가 `d3d12.h` 를 물기 때문이다.
//
// ★ **서명이 중립인 것과 헤더가 중립인 것은 다른 문제다.** §7.2.2 가
//   "중립 설명이 있어도 반환형이 DX12 면 두 번째 백엔드는 그 캐시를 쓸 수
//   없다"고 적었는데, 그 문장에 한 겹이 더 있었다 — **반환형까지 중립이어도
//   선언이 DX12 헤더 안에 있으면 마찬가지다.**
//
//   이것이 V7(이동)이 정리가 아니라 **기능**인 이유다. A-1b 가 그 사실을
//   실물로 확인한 자리이고, 옮긴 뒤에야 `VulkanPipelineCache` 가 상속했다.
//
// ── 왜 이 둘만 옮기는가 ──
//
// `IRenderDeviceServices` · `IRenderMeshCache` · `IRenderTextureCache` 는 아직
// 서명에 DX12 가 있어 옮겨도 소용이 없다(옮긴 헤더가 다시 `d3d12.h` 를 물어야
// 한다). **중립화가 끝난 것만 옮긴다** — 그것이 V7 을 조각으로 할 수 있게
// 하는 규칙이고, 이 파일이 그 첫 조각이다.

/// PSO 캐시. desc 해시로 파이프라인을 나눠 쓴다 — 뷰가 둘이어도 컴파일은 한 번이다.
class IRenderPipelineCache
{
public:
    virtual ~IRenderPipelineCache() = default;

    virtual RHIPipelineHandle GetOrCreate(
        const RHIGraphicsPipelineDesc& desc, std::string& outError) = 0;
    virtual RHIPipelineHandle GetOrCreateCompute(
        const RHIComputePipelineDesc& desc, std::string& outError) = 0;
};

/// 레이아웃 캐시. 같은 레이아웃이면 같은 객체와 같은 핸들을 돌려준다.
///
/// ★ 예전에는 `DX12RootSignatureEntry{signature, id}` 를 줬다. 호출부가 그 둘을
///   받아 하나는 멤버로 들고(SetPipeline 용) 하나는 desc 에 넣었는데(해시용),
///   A-1 이 둘 다 표 안으로 넣었다 — 앞엣것은 파이프라인 핸들이 짝으로 들고,
///   뒤엣것은 표가 안정 해시로 든다.
class IRenderRootSignatureCache
{
public:
    virtual ~IRenderRootSignatureCache() = default;

    virtual RHIPipelineLayoutHandle GetOrCreate(
        const RHIPipelineLayoutDesc& desc, std::string& outError) = 0;
};

#endif
