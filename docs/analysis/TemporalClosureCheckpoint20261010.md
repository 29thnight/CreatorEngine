# PHASE 4.5 — PR #169 종료 체크포인트 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h#L7) — `class EnhancedTemporalUpscalePass final : public EnhancedRenderPass`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h#L9) — `public:`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.h#L10) — `const char* GetName() const override { return "Temporal.Upscale"; }`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp:3](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp#L3) — `void EnhancedTemporalUpscalePass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp#L5) — `if (!m_host || !m_applied) throw std::runtime_error("Temporal upscale pass has no view owner.");`
- [Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/PostProcess/EnhancedTemporalUpscalePass.cpp#L10) — `throw std::runtime_error("Native temporal fallback must render at display resolution.");`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 판정과 범위

DX12 RTX 2080 Ti의 SDK 활성 Debug/Release 실행을 기준으로 TR1(8인일)과 TU5(4인일)를 종료한다. 활성 16행 중 2행 완료, 추정 71·기성 12·잔여 59인일(17%)이다. 전체 원장은 추정 563·기성 138·잔여 425인일이며 별도 미산정은 유지한다. 다른 14행은 소스 구현과 부분 실행 증거가 있어도 완료선 전체를 통과하지 않았으므로 progress·기성 0이다.

TR1은 실제 자산, 직전 승인 제출, 입력 hash, positive/disabled-control GPU 픽셀, indexed/meshlet 경로를 검사하는 7-route strict motion oracle로 종료한다. 구성별 14 GPU 캡처·63 probe이며 실제 temporal history 계약 495단정도 실행한다. 분석적 입력만으로 모션 생산 완료를 인정하지 않는다.

TU5는 전용 `EnhancedTemporalUpscalePass`가 PostChain 앞에 있고 UI가 PostChain 뒤에 있는 실제 compiled graph와 GPU 출력으로 종료한다. native·FSR 4품질·DLSS Quality·XeSS Quality·native의 8전환에서 요청/활성 provider 일치, 고정 카메라/물체 경계와 표시 해상도 grid coverage를 검사한다. 별도 `dx12.ui` GPU 픽셀 회귀도 검사한다. 이 판정은 전체 SDK 화질 행렬이나 FG HUD 합성 수용을 의미하지 않는다.

이 체크포인트의 로컬 실행 정본은 `Artifacts/PR169/closure-checkpoint-20261010.json`이다. 각 실행은 독립 디렉터리에 보존한다. native profile/motion·입력 소비·SDK 실행의 runtime DLL SHA256을 최종 바이너리와 대조하고, overlay/mixed 실행 경로도 같은 구성의 정본에 묶었다. 원본 실패 아티팩트를 수정하지 않는다.

## 구현과 결함 수정

- Temporal 입력은 SSGI, 실제 upscaler, 요청 FG, Player export 또는 진단 캡처가 소비할 때만 선언한다. native·SSGI OFF에서는 입력 묶음과 SSGI 13패스를 생략한다. OFF→ON 재진입은 history를 다시 시작한다. 고정 MRT PSO를 유지해야 하는 개별 geometry attachment는 임의로 제거하지 않는다.
- RenderGraph 구조 스케줄은 바이트 단위 계약 일치 때만 재사용한다. transient 생성·현재 GPU 자원·배리어·compile generation은 매 프레임 새로 처리한다. CPU 전용 불투명 metadata 2개를 per-view에 보존해 GPU graph 은퇴 후에도 SSGI history ping-pong 두 형태를 재사용한다. 크기/패스 수 상한과 설정 OFF 경로를 유지한다.
- GPU profiler의 동일 이름·동일 timestamp scope 충돌을 제출 내 scope ordinal로 분리했다. marker와 measurement는 같은 이름을 사용하며 원래 타이밍과 제출 신원은 유지한다. strict oracle을 약화하지 않았다.
- DLSS actual reconstruction provenance가 SDK 품질 fixture 승인 조건에서 빠져 있던 결함을 수정했다. 실제 DLSS HDR/readback·예열 최소 32관측·이동 8회·회복 8회를 기존 임계값으로 판정한다.
- Release는 SDK 컴파일 옵션만 활성이고 runtime DLL 배포 옵션이 빠져 있었다. `CreatorDeployTemporalRuntime=true`로 Editor/Temporal의 신뢰된 Streamline·DLSS·XeSS DLL을 배포하고 실제 provider 일치 검사를 다시 실행했다. 이전 fallback 실패는 보존한다.

## 검증 내용

최종 Debug runtime SHA256: `35D9EBEE53C1942D7734F86DB6702920382CA657154B5A57673C3AA78FF66C5A`.

최종 Release runtime SHA256: `F71C118675983BE5809B959FEE7F89BF7F17D3F25F020CBE22CB9B9DC3E0788D`.

| 검사 | 실행 수용 범위 |
|---|---|
| SDK 활성 빌드 | DX12 Debug/Release, 런타임 DLL 배포 포함 |
| Native 모션/history | 구성별 7 route·14 GPU 캡처·63 probe, history 495단정 |
| 입력 소비/SSGI/UI | native GI → native bypass → actual DLSS/no GI → native GI 복구; 자원 76/55/60/76, temporal pass 1/0/1/1, SSGI pass 13/0/0/13; SSGI·UI GPU 회귀 |
| Editor overlay | 구성별 8전환, actual provider 일치·고정 카메라·표시 grid·compiled graph 순서 |
| Mixed model rendering | Scene/Game에서 actual DLSS, rigid/skinned 4 draw 유효성 |
| DLSS warmed Quality | 구성별 실제 SDK 17 GPU 캡처·Debug 32/Release 33 예열 관측, 기존 HDR/display 임계값·provenance·normal exit |
| Native profiler | Debug 104 / Release 121회 실제 구조 재사용; GPU event 7,038 / 8,280개 전부 measurement 일치, loss 0, native-real gate 통과 |
| Native profiler 변이 | 실제 Debug capture의 분석 신원 변이 9개 거부; 파서 이후 판정 검사이며 wire parser 변이 증명으로 확대하지 않음 |
| DLSS sequence 변이 | 실제 Debug DLSS capture의 23개 변이 거부, 원본 bytes 보존 |
| 구조 cache GPU probe | 구성별 1,119,999단정 통과; fresh barrier·stale epoch·duplicate import·GPU graph 은퇴 후 opaque metadata 재사용 검사; `closure-cache2-probe-{Debug,Release}.log` |

최종 실행 디렉터리(`Artifacts/PR169/` 기준):

| 검사 | Debug | Release |
|---|---|---|
| Native profile | `closure-native-Debug-profile-9662b52ea0c946648605ca01b7dc41f5` | `closure-native-Release-profile-40a6c1a655b04c7b9a7562e2ae1d48c1` |
| Motion/history | `closure-native-Debug-motion-12ba6a1fcbf0429594736d1a6f0a22d4` | `closure-native-Release-motion-a741cb59d3cc42e59db30bd6aa4ebf65` |
| Input demand/SSGI/UI | `temporal-input-demand-eb18993a96fc454db8df6d3c8d3e39c3` | `temporal-input-demand-986cc544e534469f812e7362fc832b32` |
| Overlay | `creator-temporal-overlay-437f7b3e723e4fe38434d57cf6c22194` | `creator-temporal-overlay-9d9f2df435334a2590f177fdaaef17b7` |
| Mixed models | `creator-mixed-rendering-38594e9596b647f58b41cf22fff4eba6` | `creator-mixed-rendering-52a9f3081111499692aba276cdf3a754` |
| DLSS Quality | `closure-sdk-Debug-dlss-quality-8d552dba684a4a9d9a537b3a85acd88f` | `closure-sdk-Release-dlss-quality-cc0ecc1df5b849e9893188b5a9d5f877` |

basic DX12 validation을 사용하는 제품 회귀는 메시지·문제·누락 0을 요구한다. native profiler 실행에서는 계측 영향을 줄이기 위해 validation OFF를 명시하며, 이를 GPU validation 수용으로 계산하지 않는다. 별도 GPU probe는 GPU validation을 켠다.

## 실패 이력과 측정 한계

첫 cache 구현은 완료된 slot의 graph가 이미 은퇴해 제품에서 재사용 0이었다. CPU metadata를 분리한 다음에도 SSGI history의 두 logical graph 형태 때문에 단일 캐시는 재사용 0이었다. 두 형태를 보존한 최종 바이너리에서 실제 재사용을 확인했다. 스케줄 재사용 횟수는 성능 개선률이 아니다.

이전 GPU measurement mismatch 396개는 `LX.MeshWorldReuse`의 동일 이름/짧은 timestamp 충돌이었다. 현재 capture의 완전한 일치와 변이 거부로 수정 범위를 확인했다. 과거 기록을 현재 바이너리의 성능 증거로 다시 표기하지 않는다.

DLSS 검증의 이전 잘못된 runtime 경로, 누락된 provenance 승인, quit 이후 종료된 서비스 poll, HTTP에서 commandlet 전용 `dx12.ui` 요청은 각각 실패로 보존한다. UI 검사는 commandlet 경로로 옮겼고 정상 quit/exit 0을 요구한다. Release SDK 배포 누락의 최초 fallback도 별도 실패다.

master의 동일 조건 capture는 없다. 원래 사용자 `.ceprof`는 incomplete/loss가 있어 master 대비 회귀율을 계산할 수 없다. 이전 controlled CPU A/B와 이번 native 계측 신원 수용은 별도 증거다. 새 cache의 반복 ON/OFF 성능 수익, master 대비 FPS, 물리 scanout·input-to-photon 개선은 주장하지 않는다. 프레임 경계 시간과 Present 반환 시각을 실프레임 표시 FPS/물리 지연으로 치환하지 않는다.

## 다음 종료 순서

1. **TR0**: golden/모든 계측 표면·생성 프레임 신원 및 실패 변이의 전체 수용. native profiler 일부 수용만으로 종료하지 않는다.
2. **TR2·TR3·TU0·TU1**: render/display·jitter·native 기준 픽셀, view/reset/recovery 전체 계약, 기능 질의/폴백과 선택적 배포 정책. Editor Play/Stop scene mutex는 snapshot·epoch·GC 소유권을 먼저 분리해야 하며 단순 unlock으로 해결하지 않는다.
3. **TU2·TU3·TU4**: 새 바이너리로 전 provider/품질/해상도의 warmed/startup/reset 행렬. 최초 물체 등장·원래 cold-motion 실패 조건·silhouette·다른 재질·variable-time 품질도 수용한다. 이번 DLSS Quality 1조건을 전수 완료로 확대하지 않는다.
4. **FG0~FG4**: Player의 실제 generated 출력, SDK 최종 소비 수명, pacing·resize/shutdown/failure recovery, HUD 합성과 게임 루프 지연 마커의 실행 수용. RTX 2080 Ti의 DLSS SR 성공은 DLSS FG 하드웨어 수용 증거가 아니다.
5. **TFG9**: NVIDIA/AMD/Intel/통합 GPU 지원 행렬, 동일 조건 master/현재 성능 반복 비교, 외부 물리 pacing/latency 측정. 필요한 다른 GPU/측정 장비 없이 완료로 표시하지 않는다.

Vulkan 실행 비교는 PHASE 4.9가 소유한다. 과거 계획/실행 기록은 당시 스냅샷으로 보존하며 현재 판정은 본 체크포인트와 현행 공수 원장을 따른다.

</details>
