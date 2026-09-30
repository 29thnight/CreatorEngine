# LX Scene material packet

상태: MAT-7의 render owner 경계 구현. `EnhancedSceneRenderer`의 LX pass 설치 완료는 아니다.

## 1. 함께 소유하는 값

`Engine/RenderEngine/MaterialGraphScenePacket.h/.cpp`의 `SceneMaterialPacket`은
다음 값을 한 불변 객체로 보존한다.

- GUID 기반 compiled generation과 typed instance, 숫자와 CPU texture owner.
- 물리 tier/route, 별도의 coverage/queue와 해당 fixed-function PSO.
- 평가한 지점들의 scene/view/geometry revision 및 normal/tangent/view/lobe 값.
- 환경 generation/owner와 정확한 입력으로 구운 Core/Layered IBL buffer.
- 해당 recording의 uniform/texture/sampler binding.

`SceneSurfaceEvaluation`의 instance는 **평가한 입력의 정본**이다. host는 그 instance와
실제 공간/시선에서 지점을 평가한 뒤 전달한다. 이 경계는 graph를 평가하거나 지점을
보간하지 않으며, 전달한 값이 geometry의 어느 픽셀인지 추측하지 않는다.
scene/view/geometry revision은 모두 0이 아니어야 한다. 새 instance를 만들면서 이전
평가 결과에 새 instance 이름만 붙이는 것은 host 계약 위반이다.
geometry revision은 mesh generation뿐 아니라 world transform과 pose 변경도 구분해야 한다.
실제 mesh/animation generation owner는 이를 포함한 draw packet이 별도로 보존한다.

[MaterialGraphSurfaceBatch.md](MaterialGraphSurfaceBatch.md)의 GPU 평가 배치는 graph
instance와 spatial/view 입력에서 지점을 생성한다. 이 slot은 CPU point span 또는
owning GPU batch를 받는다. [MaterialGraphMeshSurface.md](MaterialGraphMeshSurface.md)의
GPU mesh source·완료 검증·게시 연결도 검사했다.
[MaterialGraphSceneInput.md](MaterialGraphSceneInput.md)에 실제 proxy/delta·draw pool/per-view
CPU 입력 밀봉을 연결했다. 실제 Scene GPU host 설치,
비동기 polling과 sample 배치/보간/예산은 별도로 마쳐야 한다.

## 2. Coverage와 물리 route

`ClassifySceneCoverage`는 기존 `EnhancedMaterialCoverage`를 그대로 사용한다.

| Coverage | 정렬 queue | Blend | Depth write |
|---|---|---|---|
| Opaque | 불투명 | 끔 | All |
| Masked | cutout | 끔 | All |
| Blended | 투명 | 켬 | Zero |

Core IOR 재질이 Forward로 선택되어도 불투명 재질은 Opaque에 남는다.
`MaterialRenderingMode::Transparent`로 물리 route를 대신하지 않는다.
double-sided는 cull None, single-sided는 Back을 요구하며, 독립 MRT blend는 이
단일 surface host의 범위에 포함하지 않는다. cutoff/face discard와 실제 alpha 평가는
설치하는 셰이더 host가 기존 `PbrCoverage.slang`을 통해 소비해야 한다.

현재 owner는 surface-only Core/Layered Forward를 받는다. Special/Volume은 필요한
transport와 composition을 소유하는 별도 packet이 연결될 때까지 거부한다.
전역 `Capabilities`의 기본값을 켜지 않았다.

## 3. Prepare와 Bind

`CreateScenePassLayout`은 host root 뒤에 지정한 IBL root SRV(기본 `t0`)를 붙이고, 그 뒤에 기존
material binding adapter의 `b2`, `t16+`, `s3+`를 붙인다. host overlap과 root 상한을
검사한다. host의 compiled VS/PS는 이 layout 및 실제 mesh/instance ABI에 맞아야 한다.
packet만으로 임의의 fullscreen shader를 제품 mesh shader로 인증하지 않는다.

`Prepare`는 verified generation/budget, tier/route, coverage/PSO, instance binding,
평가 지점과 환경을 검증하고 owning PSO와 binding을 준비한다. GPU 작업을 기록할 수
있는 texture 준비 전에 candidate owner를 보관한다. texture 준비 도중 recording이
바뀌면 현재 recording에서 재시도하도록 실패를 반환한다.

IBL은 장치·환경 metadata/generation/owner·평가 입력 전체가 정확히 일치할 때만 재사용한다.
매 recording의 binding은 새로 만들고 texture-cache 사용을 갱신한다. 환경 GPU cache
등록과 residency/use tracking은 환경 host가 유지한다. CPU Texture owner만으로 GPU
환경 eviction을 막는다고 가정하지 않는다.

`Bind`는 stale recording/descriptor와 layout 불일치를 거부한다. 이미 준비한 PSO,
material binding과 IBL root buffer를 건다. host frame/mesh/instance/light binding,
표본 index의 범위와 RenderGraph shader-read usage는 설치하는 pass의 책임이다.
이 bake는 graph 밖 immediate encoder로 기록하므로 split pass callback에서 호출하지 않는다.

## 4. 게시·실패·수명

`Prepare`의 반환 packet은 현재 recording의 후보이며 `Active`를 바꾸지 않는다.
엔진의 `OnUploadSubmitted`는 native queue 작업이 성공하기 **전에** 발생할 수 있다.
그 통지만으로 마지막 정상 패킷을 교체하지 않는다.

host가 제출 성공을 확인한 다음 정확한 recording/fence로 `PublishSubmitted`를 호출한다.
비동기 RHI에서는 `EndFrame`의 enqueue 성공과 CPU native submission 성공을 구분해야 한다.
검증 host는 `DrainSubmissions`로 CPU queue 작업의 성공만 확인하며 GPU 완료를 기다리지
않고 게시하는 CPU point 경로도 검사했다. GPU batch 경로는 제출 성공에 더해 완료 fence와
geometry/재질 readback 검증을 요구한다. 미검증·실패 후보는 마지막 정상 packet을 유지한다.
제품 host는 submission ticket과 비동기 readback 완료를 확인하는 경계에서 게시한다.
실패한 제출/확인되지 않은 fence로 이 함수를 호출하면 안 된다.
host의 제출 성공 확인보다 GPU가 먼저 끝나도 미게시 candidate는 보존한다. host는 성공이면
`PublishSubmitted`, 거부/실패이면 `RejectSubmitted`로 게시 결정을 마쳐야 한다.
거부 역시 GPU 사용 중인 자원의 조기 해제를 허용하지 않는다.

준비·PSO·bake 실패는 이전 제출 packet과 호출자가 가진 준비 packet을 유지한다.
자원 owner는 recording별로 제출 완료까지 남는다. 완전히 취소한 recording은 해제한다.
중간 제출이 새 recording을 열면, 제출한 앞부분의 owner를 보존하고 제출하지 않은
뒷부분의 owner만 취소한다. completion 0인 제출 통지는 idle 해제까지 보존한다.

서로 공유할 수 있는 native PSO handle은 이 slot이 임의로 invalidate하지 않는다.
PSO cache의 전역 invalidation/retirement는 전체 holder를 아는 renderer의 책임이다.
device/cache 파괴 전에 GPU idle에서 `ShutdownAfterIdle`과 외부 packet 해제를 완료한다.
device recreation은 같은 slot을 재초기화하고 새 binding/IBL을 준비해야 한다.

## 5. 검증 범위

```powershell
& Tools/regression/verify-material-scene-packet.ps1
```

`MaterialScenePacketProbe`는 RenderEngine/Utility_Framework에 링크한 실제 DX12 device,
root/PSO/texture cache, typed instance, SRGB texture와 Core/Layered bake/graphics draw를 쓴다.
GPU queue fence로 실행을 막아 두 제출이 실제 in-flight임을 확인한 다음, IOR/tier 교체와
이전 buffer 등록의 유지·완료 해제, 준비 실패/abort, 중간 제출/새 recording 취소,
정확한 IBL 재사용·환경 generation 변경과 stale-frame binding을 검사한다.

GPU fixture는 normal/view가 고정된 거울 지점과 균일 HDR 환경이다. 독립 Fresnel/coat
수식으로 ambient, IOR, single-scatter와 coverage 결과를 읽는다. Opaque/Masked/Blended
queue를 물리 Forward route와 별도로 검사한다. graph/host VS/PS 및 bake CS의
DXIL/SPIR-V 10개를 컴파일한다. 실제 GPU runtime은 D3D12이며 Vulkan runtime 검증은 아니다.

Debug/Release 각각 **165개 검사·16개 GPU 성분·실제 두 in-flight 제출**을 통과했다.
native CPU 제출 성공 확인보다 GPU 완료가 먼저 오는 경우와 명시적으로 거부한 게시의
자원 회수도 검사했다. D3D12 GPU validation WARNING 이상은 0건이다.
기존 `TypeTrait.h` C4189와 Debug LNK4075 외 신규 빌드 경고는 없었다.
raw 로그는 `Build/Obj/MaterialProductProbe/scene-packet-{build-,}{Debug,Release}.log` 및
`scene-packet-gate-final.log`다. RenderEngine/Utility_Framework Debug/Release 링크 결과이며
전체 Editor 빌드·GUI 결과로 세지 않는다.

이 게이트는 실제 Scene mesh/animation/light pass, texture-varying 공간·시선 lookup,
환경 MIS/수렴/실시간 예산, 환경 eviction/device recreation, async native submission
실패 주입, Editor 재개방을 검증하지 않는다. 해당 설치·검증 후에 Scene capability를 켠다.
