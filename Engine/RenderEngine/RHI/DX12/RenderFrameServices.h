#pragma once
#include <span>
#include <string>
#include <vector>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>

#include "DX12ResourceEntries.h"
#include "../RHIResourceTypes.h"      // 5a — 중립 값 타입이 여기서 갈렸다
#include "../IRenderDeviceServices.h" // 5c-4c — 중립 인터페이스가 여기서 갈렸다
#include "../RHIFormat.h"
#include "DX12Format.h"
#include "../RHIHandle.h"
#include "../RHIResourceState.h"
#include "../RHIPipelineLayout.h"
#include "../RHIPipelineState.h"
#include "../IRenderPipelineCache.h"
#include "../IRenderTextureCache.h"

class Mesh;
class DX12UploadSegmentAllocator;
class DX12DescriptorRecycler;
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
//   resources      GetDevice 49 · GetDescriptorRecycler 49 · GetUploadRing 38 ·
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
// ID3D12Device* · DX12DescriptorRecycler& 같은 것이 그대로 서명에 있다. 이 단계의
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


// ★ IRenderPipelineCache · IRenderRootSignatureCache 가 여기 있었다.
//   A-1b 에서 RHI/IRenderPipelineCache.h 로 옮겼다 — 서명이 중립이 됐는데도
//   선언이 이 헤더(d3d12.h 를 문다)에 있어서 Vulkan 이 상속을 못 했다.
//   include 는 위에 있으므로 이 헤더를 쓰던 호출부는 한 줄도 안 바뀐다.

// ★ `RHIMeshBinding` 이 여기 있었다. 5a 에서 `RHI/RHIResourceTypes.h` 로
//   갈렸다 — 중립인데 DX12 헤더에 있어서 Vulkan 메시 캐시가 못 쓴다.


// ★ `IRenderDeviceServices` 와 `IRenderMeshCache` 가 여기 있었다. 5c-4c 에서
//   `RHI/IRenderDeviceServices.h` 로 갈렸다 — 5c-3 이 DX12 반환형 12개를
//   내리면서 이미 전부 중립이 됐는데, 선언이 이 헤더(`d3d12.h` 를 문다)에
//   있어서 `VulkanDeviceResources` 가 상속을 못 했다. include 는 위에 있으므로
//   이 헤더를 쓰던 6곳은 한 줄도 안 바뀐다.
//
//   GizmoIcon 슬라이스에서 `IRenderTextureCache`도 중립 헤더로 갈렸다.
//   지금 이 파일은 DX12 구현 헤더를 한 번에 물리던 소비자를 위한 호환
//   파사드다. 공용 패스는 필요한 중립 계약을 직접 include 한다.


