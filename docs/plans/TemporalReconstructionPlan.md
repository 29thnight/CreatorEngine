# PHASE 4.5 — 시간축 재구성 계층 · Temporal Upscaling과 Frame Generation

## 2026-10-09 제품 설정·히스토리·profiler 소스 보완

Editor `editorRenderFeatures` / Player `build.renderFeatures`의 portable 기본값과 cook 후 `renderFeatures`·device 생성 전 소비, 저장값→명시적 환경 override→세션 CLI 우선순위, 요청/지원/설정/활성 MFG count 분리를 소스로 연결했다. 명시적 camera cut·unjittered projection 변경은 마지막 제출 history와 비교한다. CPU profiler frame owner와 render publication/real-frame/submission 신원을 분리하고, CPU render-submit/GPU-pass provenance sidecar를 `.ceprof` snapshot v4·stream v5 및 viewer/summary로 운반한다. live PLRD는 v3이며 legacy 기록의 provenance는 unknown으로 남긴다. development-only TU fault와 결함 활성 측정의 fail-closed 경로도 소스 범위다.

Presenter observation은 정확한 source identity를 가진 CPU Present 결과이며 물리 display timestamp·개별 generated identity·GPU/SDK 최종 소비 증거가 아니다. 외부 latency/pacing 수집·분석 소스는 작성되었으나 실행 수용은 전부 남는다. raw motion capture와 독립 analytic oracle은 있지만 **전체 route 씬 생성/제출 sequencing·probe pixel 소유권 driver는 미구현이고 TR1 checker는 INCOMPLETE**다. 수치 일치만으로 PASS하지 않는다. 단순 미실행 항목으로 축소하지 않는다.

FG fault injection/staging도 아직 미구현이며 TU capability/dispatch seam으로 전체 실패 행렬을 완료했다고 볼 수 없다. 상세 범위는 [통합 기록](TemporalVendorIntegration20261009.md#product-defaults-history-and-profiler-gap-closure--source-only-checkpoint)에 둔다. W9는 temporal/render-feature 설정 부분만 보완했으며 전체 설정/품질/제품 수용 완료가 아니다. 빌드·테스트·스크립트·native 실행 없이 정적 검토만 했고 **16행/71일·기성 0 및 모든 DX12 Debug/Release 실행 수용 미완료**를 유지한다.

## 2026-10-09 실제 입력·렌더 그래프·Player 배선 확장

벤더 중립 계약/SDK 어댑터에 이어 실제 LX 이전 오브젝트·스키닝 입력, per-view history/jitter와 렌더/표시 크기 분리, PostChain 전 TU, DX12 Player의 전체 GPU temporal packet·proxy·최종 소비 lease와 게임 루프 지연 마커를 연결하는 소스 구현을 진행한다. TR0는 캡처 강제 native 범위와 실/생성·양쪽 해상도 provenance를 실제 capture/profiler/gate에 전달한다.

단계별 관측/요청 커맨드와 실행 드라이버는 [Temporal validation](../../Tools/temporal-validation/README.md)에 둔다. 요청 접수와 renderer/Player 관측 세대를 나누며, SDK query·선택·실제 dispatch/present·완료점을 섞지 않는다. **빌드·컴파일러·셰이더·테스트·제품·성능 실행은 하지 않았으므로 아래 16행 완료선/71인일 수용을 완료로 바꾸지 않는다.**

Vulkan Player FG의 현 CPU 표시 브리지는 GPU temporal 입력/전용 present queue 배선이 없으므로 `NativePresentationInteropUnavailable`로 fail-closed한다. 이는 FSR Vulkan SDK 또는 GPU 지원 부재 판정이 아니다. Vulkan SR/native 어댑터 경계는 유지하고, Vulkan 실행 비교는 PHASE 4.9가 소유한다. 상세 소스 범위·외부 SDK 설정·미실행 검증은 [벤더 통합 기록](TemporalVendorIntegration20261009.md)을 참조한다.

## 2026-10-09 벤더 구현 착수

사용자 결정에 따라 업스케일러/FG의 **벤더 구현과 중립 공통 계약부터** 만들고, 실제 렌더 입력·그래프·Player 실행 경로는 뒤에 연결한다. 첫 구현의 SDK 정본과 제한은 [벤더 통합 기록](TemporalVendorIntegration20261009.md)에 둔다. FSR은 사용자 최종 선택인 **3.1.4 / FidelityFX SDK 1.1.4**, DLSS는 Streamline 2.14.1, XeSS는 3.0.2를 기준으로 한다.

소스 구현·정적 검토만으로 아래 TR/TU/FG 행을 완료 처리하지 않는다. 모션 입력, 렌더/표시 크기 분리와 FG 입력 수명·present 통합에는 실제 소스 경로가 있으며, 구현 누락 감사와 DX12 Debug/Release 실행 수용을 구분한다. 공개 공통 계약에는 native API 타입을 올리지 않으며, FSR의 두 API 구현은 SDK 정적 라이브러리 심볼 충돌 때문에 한 바이너리에서 하나만 링크한다.

아래 2026-09-14 지원 행렬은 당시 조사 기록이다. XeSS FG의 Arc 전용 설명은 현재 SDK에 적용되지 않는다: XeSS 2.1 이후 지원되는 타사 GPU에서도 FG가 가능하고, 3.x 추가 보간 수는 런타임 기능 질의로 판정한다. 라이브 기능 활성화나 현재 하드웨어 지원은 벤더 ID/표의 추정으로 결정하지 않는다.

## 2026-10-07 최근 병합 반영

PR #118/#120은 consumer lease를 GPU 완료까지 유지하는 표시 수명, 최신 packet/Host GPU 진입 제한과 Player native Present 기반을 적용했다. 이 변경은 FG provider/SDK·생성 프레임 구현 또는 PHASE 4.5 완료 공수가 아니다. 16행 71인일 및 TR/TU/FG 미완료 판정을 유지한다.

FG는 shell/RHI Present·pacing와 게임 루프가 소유하고 Editor viewport live_present는 삽입 지점이 아니다. 향후 provider의 최종 소비까지 lease를 연장하며 GT/실프레임과 생성 출력 신원을 구분한다. 50/250ms admission과 queue 상한은 지연/VRAM 최대 보장이 아니고, 실제 전후 지연·resize·device loss·종료 수용은 남는다. 계약: [OwnedBoundedPresentation](../design/OwnedBoundedPresentation.md), [PlayerNativePresentation](../design/PlayerNativePresentation.md).

근거: [10월 4~7일 PR 적용 감사](../analysis/MergedPrReview20261007.md). 아래 과거 날짜의 검증 기록은 해당 시점의 증거이며 최신 HEAD의 통과를 뜻하지 않는다.

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


**신설 2026-09-14 · `BASE-0` 이관 2026-09-15 · 활성 16행 71일 · 2026-10-09 벤더 소스 구현 착수, 실행 수용 미완료**

PHASE 4.75의 GPU 설계 게이트에 `4-5 DLSS 구상` 1일이 한 줄로 있었다. 이 문서는 그 한 줄을
독립 페이즈로 분리하고, 벤더 이름 대신 **계층**으로 다시 세운다.

| 트랙 | 단일 책임 | 행 | 일 |
|---|---|---:|---:|
| **TR** | 모션 벡터·jitter·히스토리·렌더 해상도 계약 — 시간축 기반 | 4 | 21 |
| **TU** | 벤더 중립 Temporal Upscaler와 DLSS/FSR/XeSS 백엔드 | 6 | 20 |
| **FG** | present 소유권·pacing·지연 마커와 Frame Generation 백엔드 | 5 | 26 |
| 게이트 | 지원 행렬 전수·폴백 사슬·계측 단정 | 1 | 4 |
| **합계** |  | **16** | **71** |

`BASE-0`(현재 공통 밀봉 하네스 잔여 4일)은 2026-09-15에 **PHASE 4.3**으로 옮겨갔다. 이 페이즈는 그것을
**선행 입력**으로 받으며 `TR0`·`TR1`이 직접 소비한다 — 공수에서 빠졌을 뿐 의존은 그대로다.
하네스 산출물 계약은 이 문서 §5.1이 계속 기술한다.

---

## 0. 이 페이즈가 생긴 이유

`4-5 DLSS 구상`은 두 가지를 동시에 틀렸다.

**첫째, 벤더 이름을 기능 이름으로 썼다.** DLSS는 구현이지 기능이 아니다. NVIDIA가 아닌
하드웨어에서 무엇을 하는지 계획서가 답하지 못했다. 당시(2026-09-14) 조사에서는 `FSR`·`XeSS`·`FidelityFX`·`TSR` 구현이
저장소 전체에 **0건**이었다. 현재 PR #169의 SDK/제품 배선 소스와 혼동하지 않는다. [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md) §10.4에
*"DXR/DLSS 같은 조건부 Pass는 모든 지원 행렬에 fallback 또는 명시적 pipeline invalid 사유를
가져야 한다"*는 규칙은 이미 서 있었지만, 행렬의 나머지 칸이 비어 있었다.

**둘째, 업스케일과 프레임 생성을 한 항목에 묶었다.** 업스케일러는 렌더 그래프 안의 Pass다.
프레임 생성은 Pass가 아니라 **swapchain/present 계층을 가져간다.** 소유 계층이 다르고,
완료선이 다르고, 계측에 끼치는 해가 다르다. 한 항목 1일에 둘을 넣으면 둘 다 설계되지 않는다.

그리고 셋째로, 당시에는 둘 다 같은 선행을 요구하는데 **그 선행이 이 엔진에 없었다.** 현재의 구현/미구현/실행 미검증 상태는 문서 상단의 최신 기록을 따른다.

---

## 1. 정본 경계

| 범위 | 정본 |
|---|---|
| PHASE 4.5 순서·공수·완료선 | 이 문서 |
| 페이즈 간 순서와 총공수 | [`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md) |
| C# Pass 저작·조건부 구성·fallback 계약 | [`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md)·[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md) |
| versioned resource·DAG·barrier·queue/fence | [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) |
| 재질 의미·pre-tone linear HDR 응답 | [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) |
| vertex attribute schema·model generation | [`ModelAssetBigBangCutoverPlan.md`](archive/ModelAssetBigBangCutoverPlan.md) |

이 문서는 **재질 의미도, Pass topology도, RenderGraph resource lifetime도 소유하지 않는다.**
시간축 입력을 생산하고, 화면 해상도와 present 소유권을 정하고, 그 위에 벤더 백엔드를 꽂는
책임만 갖는다.

---

## 2. 현재 코드의 사실 — 2026-09-14 실측

### 2.1 시간축 입력이 없다

`Engine/RenderEngine/Render/Passes/` 아래 Geometry·Lighting·PostProcess·UI를 전수 확인했다.

- **모션 벡터 생산 패스 0건.** velocity 채널도, 별도 패스도 없다.
- **TAA 0건, jitter 0건.** projection에 sub-pixel offset을 넣는 자리가 없다.
- 시간축 히스토리를 가진 유일한 패스는 SSGI다. `m_history[2]`·`m_historyDepth[2]`와
  `m_previousViewProjection`으로 **depth 재투영**을 한다
  (`EnhancedSSGIPass.h` 227~252행).

SSGI의 방식은 **정적 장면 전용 근사**다. 이전 view-projection으로 되쏘는 것이므로 스킨드
메시나 움직이는 오브젝트의 자기 운동이 담기지 않는다. 업스케일러에 그대로 넘기면 움직이는
물체에서 고스팅이 난다. **재사용 대상이 아니라 흡수·교체 대상이다.**

### 2.2 UI 합성 위치는 유리하다

현재 순서는 이렇다.

```text
GBuffer/Deferred/Forward (HDR) → Sprite → SSAO/SSGI/SSR/SSS/Fog
    → PostChain (HDR → LDR RGBA8) → UI (LDR) → live_present
```

`EnhancedSceneRenderer.cpp:1523`이 *"화면 UI는 PostChain 뒤 LDR에 합성한다"*를 명시한다.
`EnhancedUIPass`가 독립 패스이고 이미 포스트 체인 뒤에 있다 — TU와 FG 양쪽이 요구하는
"UI는 재구성 이후"가 구조적으로 이미 만족돼 있다. **이 페이즈에서 가장 운이 좋은 지점이다.**

### 2.3 present는 셸이 독점하고, 에디터 뷰포트는 present를 타지 않는다

- `DX12DeviceResources.h:156` `Present()`, 495행 `m_swapChain` — 주석이
  *"셸 전용 — AttachSwapChain을 부른 인스턴스만 갖는다"*.
- 에디터 뷰포트의 `live_present` 패스는 **swapchain present가 아니라** 공유 텍스처
  `CopyTexture` 또는 `CopyToReadback`이다 (`EnhancedSceneRenderer.cpp:2432`).

이것이 FG 트랙의 경계를 그대로 정해 준다. **FG는 셸 swapchain에서만 성립하고 에디터
뷰포트에서는 성립하지 않는다.** 정책이 아니라 구조가 그렇다.

### 2.4 해상도 이원화는 방금 데인 축이다

2026-09-14 세션에서 라이브 렌더러가 씬 렌더 전에 해상도를 바꿔 FPS가 반토막 난 일이 있었다.
업스케일은 **렌더 해상도 ≠ 표시 해상도**를 제품 불변식으로 도입한다. 그 축이 아직 안정되지
않은 상태에서 업스케일러를 얹으면 원인 판별이 불가능해진다. `TR2`가 이 부채를 먼저 갚는다.

---

## 3. 계층 분리 결정

### 3.1 세 계층은 소유 대상이 다르다

| 계층 | 소유 | 사는 곳 | 없으면 |
|---|---|---|---|
| **TR** 시간축 기반 | 모션 벡터·jitter·히스토리·renderScale | 렌더 그래프 생산 측 | TU도 FG도 불가능 |
| **TU** 업스케일 | 재구성 Pass 하나 + 벤더 백엔드 | 렌더 그래프, PostChain **앞** | 원해상도 렌더 |
| **FG** 프레임 생성 | present 소유권·pacing·지연 마커 | RHI/셸 + 게임 루프 | 실프레임만 표시 |

**TR의 공수가 이 페이즈의 절반 가까이다.** 벤더 SDK를 붙이는 일이 아니라 모션 벡터를 만드는
일이 본체다. NVIDIA든 AMD든 이 비용은 똑같이 낸다. 그리고 이걸 깔면 TAA·모션 블러·SSGI
품질 개선이 함께 열린다 — TR은 TU/FG가 취소돼도 남는 자산이다.

### 3.2 자체 TSR을 만들지 않는다

언리얼은 벤더 중립 고품질 재구성을 위해 TSR을 직접 만들었다. 우리는 만들지 않는다.

**FSR이 이미 벤더 중립이기 때문이다.** FSR 2/3의 업스케일러는 순수 컴퓨트 셰이더이고
하드웨어 잠금이 없으며 MIT 소스다. 벤더 중립 경로가 공짜로 생기는데 자체 TSR을 쓰는 것은
YAGNI다. `TU1`은 완전한 TAAU가 아니라 **무-upscale 기준 경로**만 만든다 — 폴백의 마지막
칸이자 A/B 대조군이다.

이 결정이 뒤집히는 조건은 하나뿐이다. FSR을 실을 수 없는 플랫폼이 생기는 경우. 현재
소비자 0이다.

### 3.3 FG는 `Q0`를 선행으로 받지 않는다

FG의 보간 작업이 async compute를 요구하므로 `Q0`(queue/fence RHI 계약)가 선행처럼 보인다.
**아니다.** 우리는 자체 보간기를 만들지 않는다. FidelityFX의 frame interpolation swapchain도
NGX의 DLSS Frame Generation도 **SDK가 자기 큐와 자기 present 타이밍을 소유한다.** 우리가
넘기는 것은 디바이스와 present 소유권이지 큐 스케줄이 아니다.

따라서 PHASE 4.5는 PHASE 4.3의 `RG6`·`Q0`를 기다리지 않는다. **이 전제가 뒤집히는 경우는
자체 보간기를 쓰기로 결정할 때뿐이고, 그때는 `FG0` 공수가 다시 산정돼야 한다.**

---

## 4. 벤더 지원 행렬

### 4.1 업스케일 축

| 벤더 | 기술 | 하드웨어 잠금 | 라이선스 |
|---|---|---|---|
| 전체 (베이스라인) | FSR 2/3 | **없음** — 컴퓨트 셰이더 | MIT, 소스 벤더링 |
| AMD RDNA4 | FSR 4 | RDNA4 (ML) | FidelityFX SDK 경유 |
| NVIDIA RTX | DLSS Super Resolution | RTX (Tensor Core) | NVIDIA SDK, 재배포 조건 |
| Intel | XeSS | DP4a 경로 범용 / XMX는 Arc | 바이너리 SDK |
| 폴백 | 무-upscale | — | — |

### 4.2 프레임 생성 축

| 벤더 | 기술 | 하드웨어 잠금 |
|---|---|---|
| 전체 (베이스라인) | FSR 3 Frame Generation | **없음** — NVIDIA/Intel에서도 동작 |
| NVIDIA | DLSS Frame Generation | **Ada (RTX 40) 이상** — Optical Flow Accelerator |
| NVIDIA | DLSS Multi Frame Generation | **Blackwell (RTX 50) 전용** |
| Intel | XeSS 2 Frame Generation | Arc 전용 |
| 드라이버 | AMD AFMF · NVIDIA Smooth Motion | 엔진 통합 대상 아님 |
| 폴백 | 생성 없음 | — |

### 4.3 두 축에서 같은 결론

**AMD 대응을 위한 별도 분기를 만들지 않는다.** 두 축 모두 AMD 기술이 벤더 중립이므로
**FSR을 베이스라인으로 깔고 DLSS/XeSS를 그 위에 조건부로 덮는다.** 폴백 사슬의 끝이
"기능 없음"이 아니라 "FSR"이 되므로 지원 행렬의 빈칸이 사라진다.

FSR 3.1부터 FG 모듈이 업스케일러에서 분리됐다. **두 축은 독립적으로 고를 수 있어야 한다** —
DLSS 업스케일 + FSR 3 FG 조합이 성립하고, 이 조합을 구조가 막으면 안 된다.

### 4.4 선택은 벤더 ID가 아니라 기능 질의로

`0x10DE`/`0x1002`/`0x8086` 분기를 금지한다. 드라이버 버전·노트북 MUX·가상화 어댑터에서
벤더 ID는 거짓말을 한다. **런타임 기능 질의에 성공한 후보만 사슬에 남긴다.** 이 저장소의
기존 판정 규약과 같다 — 판은 런타임이 찍는 값으로 정한다.

DX12 한정으로는 Microsoft DirectSR이 이 추상화를 OS 레벨에서 제공하는 방향이다. 다만
2026-09-14 기준 preview 단계이고 Vulkan에 대응물이 없다. **우리 추상 계층은 어차피 필요하며,
DirectSR은 DX12 백엔드의 구현 수단 후보로만 둔다** — `TU0`가 판정한다.

---

## 5. 작업 표 — 71일 + 선행 `BASE-0`

### 5.1 선행 입력 — `BASE-0` (PHASE 4.3 소유, 잔여 4인일)

| ID | 내용 | 상태 | 선행 | 일 | 소유 |
|---|---|---|---|---:|---|
| `BASE-0` | 공통 밀봉 frame/PNG/HDR/timing/graph stats | · | 구현·검증된 MAT-0~MAT-8 기반 (MAT-9 최종 수용 제외) | 4 | **PHASE 4.3** |

**이 행은 이 페이즈의 공수에 포함되지 않는다.** 2026-09-14에 PHASE 4.75에서 이곳으로 이관했고
2026-09-15에 PHASE 4.3으로 다시 옮겼다 — 소비자 중 4.3 `RG1`/`RG6`가 가장 앞에 서기
때문이다. 이후 분리된 4.6~4.8도 필요한 게이트에서 같은 하네스를 소비한다. **하네스 산출물 계약 서술은 이 문서가 계속
소유한다** — 밀봉 frame packet·PNG/HDR·timing·graph stats의 형식과 계측 무해화 요구가 `TR0`의
직접 입력이라 여기서 갈라 놓으면 두 벌이 된다. 소유 페이즈·공수·선후는
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)가 정본이다.

### 5.2 트랙 TR — 시간축 기반, 21인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TR0` | 계측 무해화 계약 — 모든 측정 표면의 프레임 종류 선언 | · | BASE-0 | 3 |
| `TR1` | 모션 벡터 생산 — static/skinned/instanced/decal/알파 | · | BASE-0 | 8 |
| `TR2` | 렌더 해상도 ≠ 표시 해상도 계약·jitter 시퀀스 | · | TR1 | 5 |
| `TR3` | 히스토리·이전 프레임 행렬 공용 계약, SSGI 자체 히스토리 흡수 판정 | · | TR2 | 5 |

`TR0`가 트랙 첫 항목인 것은 실수가 아니다. §7을 참조한다.

### 5.3 트랙 TU — 업스케일, 20인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TU0` | 벤더 중립 Upscaler 인터페이스·런타임 기능 질의·폴백 사슬·DirectSR 판정 | · | TR3 | 3 |
| `TU1` | 무-upscale 기준 경로 — 폴백 마지막 칸이자 A/B 대조군 | · | TU0 | 2 |
| `TU2` | FSR 백엔드 — 벤더 중립 베이스라인 | · | TU0 | 5 |
| `TU3` | DLSS Super Resolution 백엔드 (NGX) | · | TU2 | 3 |
| `TU4` | XeSS 백엔드 | · | TU2 | 3 |
| `TU5` | PostChain 앞 삽입·슬롯 schema 크기 유도·UI 합성 위치 재확인 | · | TU2 | 4 |

`TU3`·`TU4`가 `TU2` 뒤에 오는 것은 의존이 아니라 **순서 규약**이다. 벤더 중립 경로가 먼저
서야 벤더 백엔드가 "추가 경로"가 되고, 그 반대면 벤더 백엔드가 기준이 되어 폴백이 2등 시민이
된다.

### 5.4 트랙 FG — 프레임 생성, 26인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `FG0` | present 소유권 이전·proxy swapchain·frame pacing 계약 | · | TU5 | 8 |
| `FG1` | 지연 마커 — Reflex·Anti-Lag 2, 게임 루프 경유 | · | FG0 | 4 |
| `FG2` | FSR 3 Frame Generation 백엔드 — 벤더 중립 베이스라인 | · | FG0 | 6 |
| `FG3` | DLSS Frame Generation 백엔드 | · | FG2 | 4 |
| `FG4` | HUD-less 백버퍼·UI 합성·**에디터 금지 강제** | · | FG2 | 4 |

`FG1`은 렌더 트랙 안에서 끝나지 않는다. 지연 마커는 시뮬레이션 루프에 박혀야 하므로
3계층 분리(L0~L4) 경계를 가로지른다. **이 항목만 다른 계층의 승인을 받는다.**

### 5.5 통합 게이트 — 4인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TFG9` | 지원 행렬 전수·폴백 사슬·계측 단정·성능/지연 판정 | · | TU3, TU4, FG3, FG4 | 4 |

---

## 6. 실행 순서

```text
PHASE 4.25 MAT-0~MAT-8 구현·검증 기반 (MAT-9 열린 상태)
    ↓
PHASE 4.3 BASE-0  (이 페이즈가 소유하지 않는다 · RG 본체는 기다리지 않는다)
    ├─ TR0 (계측 무해화) ──────────────────┐
    └─ TR1 → TR2 → TR3 → TU0 → TU1        │
                          ├→ TU2 → TU3     │
                          │      ├→ TU4    │
                          │      └→ TU5 → FG0 → FG1
                          │                 ├→ FG2 → FG3
                          │                 │      └→ FG4
                          └─────────────────────────→ TFG9
    ↓
PHASE 4.6 C# 저작 · 4.7 라이트맵 · 4.75 렌더러 품질 · 4.8 GPU 설계
(각 항목이 필요한 BASE-0·RG·Q0 계약을 PHASE 4.3에서 입력으로 받는다)
```

`TR0`는 `TR1`과 병렬이지만 **`TU2` 착지 전에는 반드시 끝나 있어야 한다.** 업스케일러가 붙는
순간부터 모든 성능 수치가 두 해상도 중 무엇을 잰 것인지 물어야 하기 때문이다.

---

## 7. 계측 무해화 — 이 페이즈의 첫 단정

이 저장소에서 가장 위험한 지점이다. TU/FG는 **측정 하네스 전체를 동시에 눈멀게 한다.**

| 표면 | TU를 켜면 | FG를 켜면 |
|---|---|---|
| FPS·frame time 게이트 | 저해상도 렌더를 원해상도로 읽음 | **생성 프레임을 세어 수치 2배** |
| golden 이미지·`render.pbr.capture` | 재구성된 픽셀이 제품 픽셀과 다름 | **보간 프레임은 제품 프레임이 아님** |
| 프로파일러 pass GPU 시간 | pass별 해상도가 갈림 | **생성 프레임엔 pass가 없음** |
| 성능 예산 | 기준값의 축이 바뀜 | 축 자체가 무의미 |

이 저장소에는 이미 같은 실패 양식이 반복 기록돼 있다 — *벤치가 모형만 재고 있었다*,
*게이트가 초록이어도 눈멀 수 있다*, *예산이 붉은 원인은 축 불일치일 수 있다*. **TU/FG는 그
세 가지를 한 번에 유발한다.**

따라서 `TR0`의 산출물은 코드가 아니라 **계약**이다.

1. 모든 측정 표면은 **렌더 해상도·표시 해상도·프레임 종류(실/생성)를 산출물에 적는다.**
   적지 않는 표면은 게이트에서 실패한다.
2. golden·픽셀 비교 게이트는 **TU/FG를 강제로 끈 상태에서만 유효**하다. 끄는 것은 게이트가
   하고 사용자 설정에 의존하지 않는다.
3. 성능 게이트는 **실프레임 frame time**을 1급 지표로 삼고, 생성 프레임 포함 수치는 별도
   축으로만 보고한다. 두 값을 한 칸에 쓰지 않는다.
4. `TR0` 자체를 **변이로 증명한다.** 해상도 표기를 지운 산출물과 생성 프레임을 실프레임으로
   센 산출물 각각에 대해 게이트가 붉어져야 한다. 붉어지지 않으면 `TR0`는 완료가 아니다.

---

## 8. 완료 기준

### 트랙 TR

- 모션 벡터가 static·skinned·instanced·decal·알파 경로 전부에서 생산되고, 합성 fixture로
  각 경로의 벡터 방향/크기를 독립 검증한다.
- jitter 시퀀스가 결정적이고, 같은 시드에서 같은 프레임열을 낸다.
- 렌더 해상도와 표시 해상도가 분리된 뒤에도 `renderScale = 1.0`에서 **기존 픽셀과 동등**하다.
- 모든 측정 표면이 해상도·프레임 종류를 선언하고, 선언 누락 변이가 게이트를 붉게 만든다.

### 트랙 TU

- 지원 행렬의 모든 칸이 채워져 있고, 각 칸이 성공 경로 또는 **명시적 invalid 사유**를 갖는다.
- 벤더 ID 기반 분기가 소스에 0건이다. 선택은 런타임 기능 질의 결과로만 이뤄진다.
- 어떤 백엔드에서도 폴백 전후 출력 슬롯의 **포맷·의미가 같다.** 크기는 `renderScale`에서
  유도되며 schema에 하드코딩되지 않는다.
- DX12에서 같은 단정을 통과한다. backend 중립 capability와 fallback 의미를 유지한다.
- 업스케일러 선택과 FG 선택이 **독립적으로** 가능하다.

### 트랙 FG

- 셸 swapchain에서만 활성화되고, **에디터 뷰포트에서 활성화 시도가 fail-closed**한다.
- UI가 보간 대상에 포함되지 않는다. HUD-less 백버퍼 또는 UI 마스크 경로가 검증된다.
- 지연 마커가 시뮬레이션 루프에 있고, FG on/off의 입력 지연 차이가 실측된다.
- 생성 프레임이 pacing 창 안에서 균등 간격으로 present된다.
- **FG를 켠 상태에서 모든 golden·픽셀 게이트가 자동으로 FG를 끄고 판정한다.**

### 페이즈

- 위 세 트랙의 완료 기준이 모두 통과한다.
- NVIDIA·AMD·Intel·통합 GPU 각각에서 폴백 사슬이 끝까지 도달하고, 어느 하드웨어에서도
  "기능 없음"이 아니라 최소 FSR 또는 무-upscale에 착지한다.

---

## 9. 금지하는 재혼합

- **벤더 이름을 기능 이름·Pass 이름·설정 키에 쓰지 않는다.** DLSS는 백엔드 하나의 이름이다.
- 자체 TSR/보간기를 만들지 않는다. §3.2의 조건이 성립하기 전까지 소비자 0이다.
- 업스케일 품질 저하를 재질·조명 수정으로 덮지 않는다. 시간축 결함과 재질 결함은 다른
  게이트에서 판정한다.
- **TU/FG 성능 이득으로 렌더 성능 회귀를 가리지 않는다.** 성능 게이트는 `renderScale = 1.0`
  실프레임을 1급 지표로 유지한다.
- 에디터 뷰포트에 FG를 넣지 않는다. `live_present`가 swapchain present가 아니라는 구조적
  사실을 우회하지 않는다.
- PHASE 4.3의 RenderGraph resource lifetime이나 queue 계약을 이 페이즈가 재정의하지 않는다.
- SSGI의 depth 재투영 히스토리를 모션 벡터 대용으로 쓰지 않는다. 정적 장면 전용 근사다.

---

## 10. 미해결 결정

`TU0`·`FG0` 착수 전에 답해야 한다.

| 질문 | 판정 시점 | 뒤집히면 |
|---|---|---|
| DirectSR을 DX12 백엔드 구현 수단으로 쓰는가 | `TU0` | `TU3`/`TU4` 공수 재산정 |
| FSR 소스 벤더링인가 FidelityFX SDK 바이너리인가 | `TU2` | 오픈소스 재배포 조건이 갈림 |
| DLSS/XeSS SDK 재배포 조건이 이 저장소 라이선스와 양립하는가 | `TU3`/`TU4` | 해당 백엔드 선택적 빌드로 격리 |
| 자체 보간기를 쓸 가능성이 있는가 | `FG0` | `Q0`가 선행으로 들어오고 `FG0` 재산정 |
| Vulkan의 FG proxy swapchain·SDK 지원 비교 | PHASE 4.9 BP-0/BP-2 | FG0 완료 조건에서 제외; 지원/미지원·fallback을 4.9에서 판정 |

---

## 11. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-15 | `BASE-0` 1행 6일이 **PHASE 4.3**으로 이관됐다. 이 페이즈는 17행 92일 → **16행 86일**이며 `BASE-0`을 선행 입력으로 받는다. 하네스 산출물 계약 서술(§5.1)은 `TR0`의 직접 입력이라 이 문서가 계속 소유한다. 소유 페이즈·공수·선후 정본은 `Phase4UnifiedPlan.md` §6.1. **이 페이즈의 TR/TU/FG 공수·선후는 하나도 바뀌지 않았다** |
| 2026-09-14 | 신설. PHASE 4.75의 `4-5 DLSS 구상` 1일을 PHASE 4.5로 분리하고 TR/TU/FG 세 트랙 17행 92일로 재산정. `BASE-0` 6일을 PHASE 4.75 공통·GPU 설계 게이트(현 §7.1)에서 이관 |
