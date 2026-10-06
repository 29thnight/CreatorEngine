# GPU-driven / RenderGraph 전환 기록

상태: **2026-10-05 요청 범위 소스 구현 완료·동결, 영역별 및 전체 diff 독립 최종 정적 검토 완료. 신규 acceptance 전부 미실행.** 코드·정적 검토 완료와 실행 수용을 구분한다. 게시 반영 여부는 PR #122 커밋 이력과 원격 head를 대조해 별도 확인한다.

착수 기준: `master`의 `29024ddf245575360c4e43471fc13d8858798439` (2026-10-05). 작은 정적 검토 묶음으로 같은 PR #122에 전달한다.

## 2026-10-05 소스 구현 동결 범위

| 영역 | 코드에 반영한 범위 | 수용 경계 |
|---|---|---|
| RHI indexed-indirect | 단일 indexed-indirect 계약, DX12/Vulkan native 구현, encoder의 capability·buffer usage·offset/range 검사 및 미지원 direct fallback | `IndirectArgument` 전이는 graph가 소유하며 native encoder가 현재 resource state를 검증하는 것은 아니다. multidraw·indirect count·nonzero first-instance 지원을 가정하지 않으며 backend 실행은 미검증 |
| GPU geometry visibility | static opaque/masked의 reset→frustum cull·atomic compaction→간접 인자 생성, compacted visible ID/argument 버퍼, Enhanced GBuffer와 LX.Scene.GBuffer 소비 | CPU material/PSO bin과 LX per-geometry bin 유지. 선택적 화면 밖 추가 후보는 기존 draw 예산으로 제한하고 확장된 Graph Seal 실패 시 원래 visible/caster 집합으로 재시도한다. skinned/custom·미지원 direct fallback 및 기존 Seal 예산 검사를 보존하며 런타임 검증은 미실행 |
| RG5 후속 선언 | Fog·PostChain·UI·Editor Grid/WireFrame/GizmoIcon/GizmoLine의 Read/Write/Modify와 출력 버전, Decal 중복 읽기 정리 | 전체 생산 경로·test/fixture 감사와 임시 추론/adapter 0·제품 GPU 수용 미판정 |
| RG6 제품 배선 | 같은 EnhancedRenderGraph에서 ExplicitVersioned + DependencyOrder 선택, 최종 출력·capture가 최신 버전을 소비하도록 배선 | 제품 전환의 소스 변경이며 RG6 통과가 아님 |
| RG-V·진단 | generation·dependency hash·resource/version·edge·order·lifetime·barrier를 담은 immutable snapshot, Scene/Game/Material Preview reader/viewer, PBR capture의 compiledGraph schema 3(바깥 manifest schema 1 유지) | UI·세대 교체·정합성·변이·비용·메모리 검증 미실행 |
| imported 자원 상태 | 요구 final state를 마지막 소비 패스 뒤에 복구하고 snapshot/capture에 상태·배리어 공개 | 병렬/split 기록과 다음 view/frame 상태의 GPU validation 미실행 |
| runtime lookup·조명 | 사용자 승인에 따라 DX12·Vulkan live 경로 모두 split-sum 평가 사용, GPU scene/material 입력 재사용 | 정밀 적분과 기존 approximate-cache API는 bake/reference/diagnostics에 보존. 과거 DX12 정밀 경로와 픽셀 동일성은 주장하지 않음 |
| 제출 admission·수명 | reserved/admitted/native-confirmed/GPU-complete/rejected 상태 분리, never-admitted 예약의 정확한 취소와 producer 측 token 소멸 | 불명확한 native 실행의 소유권 보존·제출 차단 및 capture/resize/shutdown proof gate 구현. 실행 검증 미실행 |

코드 위치: `GpuGeometryVisibility.*`, `MaterialGraphSceneHost.*`, `MaterialGraphSceneLookup.*`,
`Render/Graph/EnhancedRenderGraph.*`, `Render/Scene/EnhancedSceneRenderer.*`,
`Render/Scene/EnhancedPbrCapture.h`, `Editor/EngineGUIWindow/EnhancedRenderDebugWindow.*`.
Engine 경로는 `Engine/RenderEngine/` 아래이며 GPU visibility·runtime lighting 셰이더는
`Dynamic_CPP/Assets/Shaders/DefaultPassShader/`에 둔다.
Scene host ABI는 **16**이며 영향을 받는 material program을 재생성해야 한다.

통합 정적 검토에서 화면 밖 추가 후보의 CPU staging 확대를 제한했다. 추가 후보 수의 상한은
기존 `SceneInputBudget.draws`(현재 4096)이며, 이는 visible/caster 집합에 더하는 선택적 후보의
상한이지 전체 입력 예산의 확대가 아니다. 확장된 Graph 입력이 draw·geometry·payload 예산
검사를 통과하지 못하면 원래 visible/caster 집합과 대응 fallback 소유자를 보존해 Seal을 다시
시도한다. 재시도도 기존 예산 검사를 통과해야 하며, 실제 대규모 장면 실행은 검증하지 않았다.

승인된 기본 live runtime lookup 전환은 소스에서 전체 화면의 11×RGBA32F 입력(176 B/pixel)과
`IblBakeSample` 출력(144 B/pixel), 합계 **320 B/pixel**의 lookup bake 저장소 할당을 제거한다.
현재 live 호출은 capture 입력을 끄고 sample 버퍼는 1개 레코드만 둔다. 정밀/reference·명시적
capture API의 저장소와 별도 특수 재질 scratch/snapshot은 이 설명에서 제외하며, 실제 4K 전체
peak VRAM·성능 수용이나 모든 메모리 문제의 해결을 뜻하지 않는다.

## 특수 재질 runtime 타일 — 소스 구현 범위와 정적 메모리 산술

- 256×256 tile에 4-pixel halo를 더한 264×264 scratch를 사용한다. 14개의 RGBA32F를 두 ordered MRT 단계(8+6)에 기록하며 float32 D32 tile-depth 초기화와 SSS halo를 연결했다
- 특수 stream마다 repeated graph node 1개를 선언한다. compiler가 tile depth→MRT 0→MRT 1→color의 4개 phase와 배리어를 계획하며, 타일 수만큼 graph node를 늘리지 않는다
- 굴절/volume ray의 화면 입력은 전역 HDR/depth를 소비한다. tile 경계로 ray 범위를 제한하지 않는다
- reference full-screen bake 경로는 보존하고 runtime 효과를 별도 소유자로 분리한다. live/replacement/scratch 자원과 제출 완료 전 수명을 함께 다룬다
- scratch payload는 264×264×(14×16+4) = **15,890,688 bytes(약 15.15 MiB)**다. 자원별 64 KiB 정렬의 ledger charge는 **16.0625 MiB**이며 타일별 상수를 별도로 더한다. 3840×2160의 4K에서는 135×256 = **34,560 bytes**다
- 굴절의 전역 HDR(RGBA16F)+depth(D32) snapshot ledger charge는 같은 4K에서 **95 MiB**다. 위 수치는 소스의 정적 산술이며 측정 peak VRAM·driver 실제 할당량이 아니다. native driver 계상은 backend 소유이며 여러 stream/frame·교체 중 자원과 다른 렌더 자원을 포함한 peak는 별도 검증한다
- 자원 압력에 대한 복구 가능한 지연/거부는 renderer 손상과 구분하되, 그것만으로 4K 효과의 메모리 수용을 주장하지 않는다
- 소스 구현·독립 최종 정적 검토와 별개로 D32 일치·halo 경계·SSS/투과 품질·4K peak memory·성능은 전부 미검증이다. 기본 live의 320 B/pixel lookup bake 저장소 제거와 실제 전체 메모리 수용은 별도 판정이다

## Admission·실패·소멸 경계

- reserved는 예약, admitted는 제출 큐의 소유권 인수, native-confirmed는 native 제출 확인, GPU-complete는 완료 증거이며 rejected와 구분한다. 예약값이나 native 제출 확인만으로 GPU 완료를 간주하지 않는다
- 정확한 예약 취소는 never-admitted 작업에만 적용한다. 제출 token은 producer 측에서 소멸시켜 worker 내부 대기/소멸과 자원 회수를 혼합하지 않는다
- native 실행 여부가 불명확하면 accepted 작업과 자원 owner를 유지하고 일반 제출을 차단한다. capture/resize/shutdown도 GPU idle 또는 실제 device-loss의 동일한 proof gate를 따른다
- 일반 budget pressure/rejection은 nonfatal이다. 드문 강제 소멸에서 GPU idle 입증과 실제 device-loss 입증이 모두 실패하고 유지 가능한 owner도 남길 수 없는 경우에만 기존 fatal invariant가 적용될 수 있다. 정상 거절이나 메모리 압력에 대한 처리가 아니다
- 위 경로의 코드·정적 검토 반영은 실행/실패 주입 수용을 대신하지 않는다

## 검증·전달 경계

- 이번 요청은 코드·정적 검토만 수행한다. 빌드, 자동 테스트, renderer 실행, capture, GPU validation, 성능 측정은 실행하지 않는다
- BASE-0·RG1~RG4·RG5-1~RG5-12의 기존 증거는 해당 시점의 수용 기록으로 보존한다. 신규 RG5 후속 이관·RG6·RG-V의 픽셀/성능 수용이나 진척 기성으로 전환하지 않는다
- 남은 수용: 전체 제품 선언/adapter 감사, ABI 16 재질 프로그램 재생성 후 현행 DX12 Debug/Release 빌드와 분리 프로세스 회귀, 결정적 order/hash·오류/변이 검사, GPU validation·정상 종료, indirect fallback/수명·admission 실패 경로, viewer generation·비용·메모리, 특수 재질/4K·MAT-9 품질·성능
- Vulkan native 구현은 RHI 중립 계약을 유지하기 위한 코드 범위다. Vulkan 실행 동등성·교차 픽셀 수용은 PHASE 4.9가 소유한다
- Mesh Shader·DXR·WorkGraph·aliasing·async queue·compiled-plan cache·shadow cache·새 light-volume subsystem은 이번 범위 밖이다
- 소스 동결과 전체 diff 독립 최종 정적 검토를 마쳤다. 게시 반영 여부는 PR #122 커밋 이력과 원격 head 대조를 통해 별도 확인하며, 작은 정적 검토 묶음의 PR 전달과 acceptance 완료는 구분한다. 테스트를 실행하지 않은 코드를 ready-to-merge 또는 품질/성능 완료로 판정하지 않는다

## 설계 참고

엔진 소스를 복제하지 않고 [Unreal RDG](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)의 명시적 의존성·graph 수명,
[Depth Material Expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/depth-material-expressions-in-unreal-engine)의 scene-depth 입력,
[Volumetric Lightmaps](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-lightmaps-in-unreal-engine)의 사전 계산 조명과 화면 크기 레코드 분리 원칙을 참고한다.
새 light-volume 구현이나 Unreal과의 품질·성능 동등성을 뜻하지 않는다.

세부 종료 게이트는 [RenderGraphDependencySchedulingPlan.md](../plans/RenderGraphDependencySchedulingPlan.md)를 따른다.
