DLSS 렌즈 후속: 다섯 품질 D/R FOV·near/far 변경 180캡처·90비교 및 no-op history 유지 80건 통과. 실제 투영·depth 반영과 cut 없는 reset을 확인했다. [근거](../analysis/TemporalDlssLens20261011.md).

DLSS 최종 빌드 후속: Quality/Native AA/Balanced 전환 D/R 및 Native AA startup 106캡처 통과. 다섯 품질의 지정 전환·startup 근거를 현재 바이너리로 통일했다. [근거](../analysis/TemporalDlssFinalBuild20261011.md).

DLSS 전환/reset 후속: 수동 노출 입력과 RT capture ready 경계를 수정하고 D/R 다섯 품질 전환·resize 120캡처, 네 품질 startup 136캡처 및 Performance/Ultra Performance warmed 68캡처 통과. 전체 품질·FG 수용은 잔여. [근거](../analysis/TemporalDlssTransitions20261011.md).

비 Quality startup 후속: Native AA/Balanced/Performance/Ultra Performance 최종 D/R 136캡처 통과. Balanced preset J 수정 및 warmed D/R 34캡처 회귀 통과. TU3 전체 품질·FG 수용은 잔여. [근거](../analysis/TemporalDlssStartup20261011.md).

Performance/Ultra Performance 후속: 기존 제품 Debug/Release 68캡처 통과, 각 모드 출력 해시 동일. 누적 다섯 품질의 warmed fixture D/R 근거 확보. startup/다른 해상도/실제 장면 등 TU3 전체 수용은 잔여. [근거](../analysis/TemporalDlssPerformance20261011.md).

Balanced 후속: 자동 fixture의 계층 변경과 UI 순회 사이 씬 구조 경계를 보완하고 최종 Debug/Release 34캡처·history 각 506 검증 완료. Performance/Ultra Performance 및 전체 수용은 잔여. [근거](../analysis/TemporalBalancedHierarchyRace20261011.md).

<!-- 2026-10-11: Native AA first-disocclusion follow-up; TU3 remains progress. -->
Native AA 후속: preset J 수정으로 Debug/Release 34캡처 및 동일 입력/출력 비교 통과. 나머지 품질 모드와 전체 화질 수용은 미완료. [근거](../analysis/TemporalDlssNativeAA20261011.md).

# PHASE 4.5 — 시간축 재구성 계층 · Temporal Upscaling과 Frame Generation

> 2026-10-11 TR0 CPU 기록 후속: Grid의 불필요한 CPU join을 제거하고 선언된 기록량/resource usages로 연속 target 분할을 배분했다. 물리 history barrier 수를 힌트로 쓴 v5의 결정성 실패를 수정하고 실패 원본을 보존했다. SDK legacy/product D/R 4빌드·8독립 프로세스·800표본·408캡처·120 population/3 comparator 거부 통과. 제품 CPU 중앙값 Debug 2.37030→2.01750ms(-14.88%), Release 1.48505→1.24070ms(-16.45%); 현재 legacy 1.64830/1.09415ms 대비 +22.40/+13.39% 잔여. 두 독립 실행 및 legacy 정규화 감소 기준 통과. 정상 제품 1 wave/1 dispatch/4 jobs·balanced 사용, GPU 순서·배리어 유지. 전후/D-R 17 attachments 차이 0·기존 full-topology 비교기 변경/카운터 예외 0. 대표 raw 4개+필요 실패만 보존, 여유 87.40GiB. 정적 DX12 fixture 범위로 TR0 progress·기성 0, PHASE 4.5 12/71 유지. [검증 기록](../analysis/TemporalRg6CpuRecordingTail20261011.md).

> 2026-10-11 TR0 CPU 기록 개선: GPU edge의 보수적 CPU join으로 직렬화된 기록을 확인하고 준비된 불변 입력의 명시적 독립 기록 계약을 추가했다. SDK legacy/product D/R 4빌드·8독립 프로세스·800표본·408캡처·120 population/3 comparator 거부 통과. 제품 CPU 중앙값 Debug 3.66040→2.37030ms(-35.24%), Release 2.25195→1.48505ms(-34.05%). 현재 legacy 1.61310/1.12330ms 대비 제품 +46.94/+32.20%는 남는다. GPU 순서·배리어 유지, 제품 전후 및 D/R 17 attachments 차이 0·GPU validation 0·exit 0. historical CPU 진단 필드 차이만 구분하고 GPU 구조/픽셀 8변이 거부. 최신 대표 raw 4개·필요 실패 기록만 보존, 여유 88.21GiB(3.50GiB dump 삭제 자동 정책 차단). 정적 DX12 fixture 범위이며 UI/actual generated/외부 계측/제품 budget 잔여로 TR0 progress·기성 0, PHASE 4.5 12/71 유지. [검증 기록](../analysis/TemporalRg6CpuRecording20261011.md).

> 2026-10-11 TR0 RG6 A/B 후속: 현재 소스 진단 legacy/product의 SDK Debug/Release 4빌드·8독립 프로세스·800실제 비용 표본·408캡처·120 population/3 comparator 변이 거부를 수용했다. A/B display 차이 0·전체 허용 초과 0, 제품 D/R 17 attachment 차이 0. CPU record Release 1.10→2.25ms(+105.5%)로 성능 개선 판정은 아니다. 대표 raw 4개·최소 진단 바이너리만 보존, 여유 92.37GiB. 현재 정적 fixture의 전체 행렬이며 역사적 lattice 전수와 구분한다. UI/actual generated/외부 계측/제품 budget은 남아 TR0 progress·기성 0, PHASE 4.5 12/71 유지. [실행 기록](../analysis/TemporalRg6ABAcceptance20261011.md).


> 2026-10-11 TR0 BASE-0 population 후속: SDK 활성 Debug/Release 재빌드·native 회귀 3종/구성, 독립 4프로세스의 실제 비용 400표본·204캡처·60 population 변이 거부를 통과했다. typed native CPU snapshot으로 텍스트 busy 측정 누락을 해결하고 scene Ready 및 자가검사 UI 격리를 강화했다. D/R 17 attachment 픽셀 차이 0. 대표 성공 2·필요 실패 2만 raw 보존, 최종 여유 93.21GiB. 전체 legacy/product A/B·UI·actual generated/외부 계측·제품 budget은 남아 TR0 progress·기성 0, PHASE 4.5 12/71 유지. [실행 기록](../analysis/TemporalBase0ConsumerWorkload20261011.md).


> 2026-10-11 TR0 측정 표면 후속: Scene GPU 캐시의 실프레임 신원·해상도·sample age, ProfilerViewer GPU/display 연결, 네 기존 픽셀 소비자와 BASE-0/RG6 저장 행 admission을 구현했다. SDK 활성 Debug/Release Editor·Viewer 빌드와 실제 DLSS 성공 후 native golden 오차 0·validation 0·lossless 기록 및 소비자 변이 검사를 통과했다. 대표 캡처 두 개만 보존하고 중복 raw 66개를 제거해 실제 약 1GiB를 회수했다. 전체 기존 workload·UI 육안·actual generated/외부 기구·제품 budget 수용은 남아 TR0 progress·기성 0, PHASE 4.5 12/71 유지. [검증 기록](../analysis/TemporalMeasurementSurfaces20261011.md).

> 2026-10-11 TR0 material 후속: 실제 Debug/Release empty·Aria 측정, lossless native 기록·정상 종료·저장 population/geometry/전경 감사 통과. 단일 offscreen view 332×182, draw 3·전경 깊이 픽셀 858; 제품 viewport/마스터 비교 수용은 별도. RG8 전체 workload·UI 육안·제품 budget·actual generated/외부 계측은 잔여이며 TR0 기성 0, PHASE 4.5 12/71(17%) 유지. [실행 기록](../analysis/TemporalMeasurementConsumers20261010.md).

> 2026-10-10 TR0 소비자 후속: native Player D/R 연속 owner 기록을 실행해 첫 경계 전 presenter admission 손실과 writer poll 필드 오류를 수정했다. SDK 활성 Editor/Player D/R 빌드·새 native 기록 loss 0·normal exit·C++/reader 회귀 및 예산 소비자의 13거부·초과 대조·Scene/Game 분리 검사를 수용했다. 외부 importer는 반복 Present의 정확한 sequence 연결을 추가해 27계약 검사와 기존 actual D/R 기록 476/429관측 연결·native의 FG-on 오표기 거부를 수용했다. material/RG8 native preflight·저장 metadata·GPU 측정 신원 결합을 강화해 새 actual D/R 각 2관측으로 62/52거부 검사를 통과했다. UI는 Scene updates/s·engine boundary time·GPU pass sum·참고선 표기로 수정했다. UI 빌드/native smoke와 육안 검증은 별도 checkpoint로 구분한다. 전체 material/RG8 workload·UI 육안/픽셀·실제 generated 신원·외부 기구 지연/표시·제품 budget 정책은 남아 TR0 progress·기성 0 유지. [소비자 검증 기록](../analysis/TemporalMeasurementConsumers20261010.md).

> 2026-10-10 TR0 후속: SDK 활성 D/R golden 캡처의 강제 native·요청 의도 복구·실제 전경·픽셀 오차 0·29변이, 실제 profiler CRC 유지 wire 변이 각 12개와 reader 회귀 10개를 수용했다. 전체 계측/예산/Player generated 신원은 남아 TR0 progress·기성 0, 완료 집계 12/71(17%) 유지. [검증 기록](../analysis/TemporalMeasurementCheckpoint20261010.md).

> 2026-10-10 PR #169: SDK 활성 DX12 Debug/Release 실행 증거로 TR1(8)·TU5(4)를 종료한다. PHASE 4.5는 16행 중 2행 완료·기성 12/71·잔여 59(17%), 전체 추정 563·기성 138·잔여 425(+별도 미산정)다. 품질 전수·FG·물리 지연/지원 행렬은 progress·기성 0을 유지한다. [종료 체크포인트](../analysis/TemporalClosureCheckpoint20261010.md). 아래 이전 기록은 당시 스냅샷이다.

## 2026-10-11 DLSS 첫 이동 잔상 후속

이전 종료 검증의 직접 활성 실패를 동일 raw로 재현하고, 같은 SDK 입력의 Quality preset J 비교로 수정했다. 최종 SDK D/R warmed/startup 68캡처·history 506단정/구성·4변이 거부 통과. 전체 TU3 품질 행렬과 PHASE 4.5 종료는 별개다. [원인 분리·수정·보존 기록](../analysis/TemporalDlssDisocclusion20261011.md)을 따른다. PHASE 4.5 12/71 유지.

## 2026-10-11 PHASE 4.5 종료 검증

SDK 전환 12방향·오류 복구 D/R 검증을 실행했으나 DLSS 직접 활성 Debug 품질 실패가 독립 2회 재현됐다. 제품 코드 원인 수정은 미완료다. 전체 상태와 원본 보존은 [종료 검증 기록](../analysis/TemporalPhase45Closure20261011.md)을 따른다. PHASE 4.5 12/71 유지.

## 2026-10-10 PR #169 실행 증거 보완

카메라 cut으로 발생한 **실제 SDK 리셋 제출 프레임 자체**의 HDR 품질을 별도 보완했다. 기존 history observation readback의 ordinal 1을 사용하며 `capturedReset=true`, `acceptedBefore=0`, `previousSubmittedRealFrameId=0`, `resetRealFrameId=sourceRealFrameId=realFrameId` 및 정확한 cut receipt/view/scene/history를 요구한다. FSR/XeSS adapter는 이 프레임의 reset을 실제 SDK reset 인수에 전달한다. 첫 관측 후 3~6프레임의 검사를 리셋 프레임 수용으로 바꾸지 않았다. 새 도구는 `Verify-TemporalResetQuality.ps1`·`Compare-TemporalResetQuality.py`다. 엔진 코드나 벤더 알고리즘 변경 없이 기존 SDK 활성 바이너리와 실제 캡처 경로를 사용했다.

정적 cube/floor의 authored glTF hash·sealed material/draw·카메라 원점 및 실제 GPU color/depth/normal 입력을 확인한다. 입력에서 2 render-pixel cardinal/corner 경계 검사와 local HDR variation 0.02 이하로 정한 smooth foreground/clear background만 측정하며, 각 영역의 input/output 최소 64픽셀·contrast 0.03을 요구한다. 리셋 sample-zero jitter (0,-1/6)로 같은 프레임 SDK 전 HDR을 대응시켜 normalized mean 0.10/p95 0.25를 검사한다. 별도 SDK stage와 reconstructed HDR attachment도 일치해야 한다. 같은 자세에서 두 차례 실제 cut을 실행하며 SSGI/HDR/final GPU display의 반복 최대 오차는 1e-6 이하다. final display의 절대 tone-map 정답을 독립 증명하는 검사는 아니다.

Debug/Release FSR Quality(Game 838x394, input 558x262), Debug XeSS Ultra Performance(1238x728, 413x243), Release XeSS Balanced(1238x728, 619x364)·Performance(1506x994, 655x433)의 5조건·10 리셋 캡처가 통과했다. 반복 SSGI/HDR/display의 최대 차이는 모두 0이고 각 실행 basic DX12 validation의 메시지·문제·누락은 0이다. 모든 실행은 자신의 Editor를 종료했다. 실제 GPU 증거의 20변이를 거부하며, 두 리셋의 HDR/SDK stage에 같은 비상수 RGB bias를 넣어 nonempty·반복성 검사를 유지한 변이도 독립 입력 대비 HDR 임계값에서 거부된다. 정본은 `Artifacts/PR169/sdk-reset/results.json`, 최종 재감사는 각 `live/reaudit.json`과 `mutation-reaudit.json`이다. 이전 `sdk-startup`의 golden/history 494/SSGI 회귀와 같은 runtime hash이며 이번 엔진 재빌드나 회귀 재실행으로 표기하지 않는다.

작은 검사 영역을 통과로 만들지 않았다. XeSS Ultra Game 972x462에서 input 전경 40픽셀, Balanced 838x394에서 20픽셀, Performance 1238x728에서 60픽셀은 최소 64에 못 미쳐 미수용으로 보존했다. Ultra 확대 최초 실행은 픽셀 판정이 통과했지만 검사 후 cleanup 연결의 `request.deadline` 때문에 전체 실행을 실패로 보존했고, 모든 reset command/poll 연결을 닫고 오프라인 판정 전에 캡처를 해제해 fresh retry를 통과시켰다. Release FSR 최초 실행은 기존과 같은 모델 이름의 입력 로드 단계에서 실패했으며 고유한 진단 자산 이름을 사용한 retry만 수용한다. 이 실패들의 원본은 변경하지 않았다. 정본의 `passed`는 선택한 5조건의 범위이며 `allAttemptsPassed=false` 및 별도 rejected attempts를 유지한다.

수용 범위는 **기존 SDK context의 live camera-cut reset**에서 측정 가능한 정적 내부 픽셀과 반복 출력이다. 새 SDK context 최초 프레임, 물체 최초 등장, 거부된 해상도의 충분한 검사 영역, 원래 opaque-quad cold-motion 실패와 동일한 초기 조건, 전체 신규 바이너리 품질 행렬, silhouette/다른 재질/variable-time/물리 scanout은 남는다.

초기 관측 구간을 별도 진단으로 기록했다. `render.temporal.motion.fixture <new-directory> sdk-startup` 및 verifier의 `-Startup`은 첫 non-reset baseline 제출을 관측한 뒤 캡처 admission을 시작한다. 별도 schema `temporal.sdk.startup.v1`에서 최소 관측 수는 1이며, renderer contention 때문에 추가 관측/제출이 생길 수 있다. 판정기는 32개 이상의 관측 또는 첫 관측에서 baseline 캡처까지 32 real-frame 이상의 간격을 거부한다. 실제 관측 수와 첫/마지막 관측에서 캡처까지의 간격을 결과에 남긴다. 기존 warmed v2와 startup은 `--require-baseline` / `--require-startup`으로 서로 거부하며 기존 32회 조건을 낮추지 않았다.

SDK 활성 Debug/Release 빌드 후 Debug XeSS Ultra Performance(Game 972x462), Release XeSS Balanced(838x394)·Performance(972x462), 두 구성 FSR Quality(838x394)의 5조건을 각 1회 실행했다. 초기 정지·8회 이동·8회 회복의 85 GPU 캡처와 80개의 캡처 간 exact predecessor를 확인했고 모두 기존 mean 0.10/p95 0.25·contrast 0.03·2 render-pixel guard를 통과했다. 관측 수는 1~2개, baseline 캡처는 첫 관측에서 3~6 real-frame 뒤였다. 이동/회복의 최악 HDR mean/p95는 0.042760/0.097987, final display는 0.072211/0.157143이다. 초기 HDR은 같은 프레임 pre-upscale 입력과 비교하고 final display 초기 검사는 contrast/균일성이다. 같은 바이너리의 Debug XeSS Ultra warmed 대조 17캡처도 통과했다. 최종 checker 재감사 결과는 각 `verification/reaudit.json`, 정본은 `Artifacts/PR169/sdk-startup/results.json`이다.

두 구성의 기존 golden 캡처·temporal history 494단정·SSGI 회귀 및 basic DX12 validation 0건을 확인했다. 초기 XeSS 실제 캡처를 이용한 25개 변이 거부, 두 모드 혼용 거부 2개와 native oracle 단위 회귀 23개도 통과했다. 실행이 소유한 Editor는 종료했다. 기존 40조건 matrix의 3실패 및 이전 아티팩트는 그대로 보존했다.

이 결과는 **첫 관측 이후의 짧은 non-reset 구간**이다. 관측 이전 SDK context의 누적 수명이나 리셋 제출 프레임, 새 물체가 처음 등장하는 프레임을 증명하지 않는다. 원래 실패와 완전히 동일한 초기 상태의 재현/수용, 전체 신규 바이너리 matrix, variable-time 및 다른 material/edge 품질도 잔여다. 벤더 알고리즘을 수정하거나 기존 실패를 통과로 바꾼 결과가 아니다.


SDK 연속 이동 진단의 초기 상태를 후속 보완했다. 기존 v1은 첫 승인 baseline을 보자마자 이동했으며 안정된 SDK 출력은 캡처하지 않았다. v2는 같은 view/scene/history의 서로 다른 non-reset 승인 제출을 최소 32개 관측한 뒤, 동일 자세의 측정 step 2를 별도 GPU 캡처한다. 초기 HDR은 같은 프레임 SDK 전 입력과 비교하며 foreground color/depth·0 모션·실제 SDK provenance·final display contrast/균일성을 확인한다. 첫 이동 step 3은 이 초기 캡처와 exact source/real predecessor가 같아야 한다. 기존 8회 이동·8회 회복 및 평균 0.10/p95 0.25·contrast 0.03·2 render-pixel guard는 유지했다. `Verify-TemporalSdkSequence.ps1`은 captured/warmed v2 baseline을 요구하고, 독립 checker의 `--require-baseline`은 기존 v1을 거부한다. 역사 기록 검토용 v1 해석은 유지한다.

final display의 기존 좌측 배경 평균 비교에는 위치별 후처리 차이가 포함된다. v2는 실제 물체가 사라진 8번째 회복 캡처의 동일 픽셀을 기준으로 비교하며, 그 캡처의 입력·pose·source/history·RGB/depth·finite bytes 및 HDR을 별도로 확인한다. 이 reference는 절대 tone-map 동등성이나 post-chain 자체의 독립 수용 증거가 아니다. 예열 변경만 적용하고 비교 위치를 바꾸기 전에도 원래 세 실패 조건의 첫 이동 평균/p95는 Debug XeSS Ultra Performance 0.067753/0.100000, Release Balanced 0.057330/0.071429, Release Performance 0.088242/0.128572로 기존 기준을 통과했다(`warmup-only-observations.json`). 초기 조건이 결과에 영향을 줬다는 실행 증거이며 벤더 SDK 알고리즘 자체를 수정한 것은 아니다.

SDK 활성 Debug/Release를 빌드하고 위 세 XeSS 조건을 각각 3회, FSR Quality 대조를 각 구성 1회 실행했다. 최종 checker로 11회·187캡처·176개의 캡처 간 exact predecessor를 재감사해 모두 통과했다. 초기 정지 캡처까지 포함한 17개 입력/출력의 신원과 상태를 검사하며 측정 이동 step은 각각 한 번만 제출한다. 이동/회복 프레임별 최대 평균/p95는 HDR 0.028018/0.054884, final display 0.048990/0.100000이다. 예열 부족/중복/히스토리 변경/캡처 이후로 위조된 예열, 초기 캡처 누락 및 HDR/모션 훼손, 회복 기준 display/HDR/color 훼손을 포함한 v2 변이 23개를 모두 거부했다. native oracle 단위 회귀 23개, 각 구성 native 7경로·14캡처·63 probe, 일반 golden 및 히스토리 계약 494단정·SSGI abort/reset 회귀도 통과했다. 각 실행 basic DX12 메시지/문제/누락은 0이며 같은 구성의 SDK/native/golden runtime hash가 일치한다.

정본은 `Artifacts/PR169/sdk-baseline/results.json`, 최종 11회 재감사는 각 `verification/reaudit.json`, 변이는 `mutations-final-xess-ultra-Debug/results.json`이다. Debug golden 최초 실행은 UTF-8 BOM이 첫 명령에 섞인 driver 실패로 보존했고 `golden-Debug-retry`에서 BOM 없는 명령 파일과 실제 exit-code 확인으로 전체 통과했다. Release 회귀는 `native-Release`·`golden-Release`, Debug native는 `native-Debug`다. 이전 40조합 중 3실패의 정본 및 원본은 유지한다. 이번 수용은 예열된 opaque quad 내부/disocclusion의 위 3조건 반복 및 FSR 대조이며, cold/appearance 품질·새 바이너리 전체 40조합·silhouette/다른 재질/카메라 연속 이동·variable-time/물리 pacing/성능의 수용을 뜻하지 않는다.

품질·해상도 확대 행렬을 실행했다. 기존 SDK 연속 opaque quad 내부/disocclusion 검사를 Native AA/Quality/Balanced/Performance/Ultra Performance로 확장하고 요청 품질→authored 입력→모든 캡처의 품질 일치를 요구한다. Native AA는 render/display 1:1, 다른 모드는 축소 입력을 요구하며 기존 contrast 0.03·2 render-pixel 경계 제외·평균 0.10/p95 0.25 기준은 유지했다. 판정 영역 순회를 최적화한 뒤 기존 Quality 및 확대 Native AA 16프레임의 측정값·attachment hash가 정확히 같음을 확인했다. 화질 실패는 전체 16프레임의 `qualityFailures`로 남기되 `passed=false`·종료 코드 1을 유지한다.

DX12 Debug/Release × FSR/XeSS × 다섯 품질 × 두 host 크기 1400x900/1600x1000의 40조합을 완료했다. 실제 Game display는 838x394/972x462이며 총 640 GPU 캡처·600개 캡처 간 exact predecessor 연결, 모션 1,333,840 probe·배경 노출/회복 3,794,356픽셀을 검사했다. FSR 20조합은 통과, XeSS 20조합 중 17통과·3실패다. 실패는 모두 첫 이동 프레임의 final GPU display residual이다: Debug Ultra Performance 972x462는 평균 0.147778/p95 0.328572, Release Balanced 838x394는 0.162587/0.228572, Release Performance 972x462는 0.111968/0.200000이다. 세 실패의 이후 15프레임과 HDR은 기준 안에 있으며 실패 기록을 교체하지 않는다. 최초 baseline은 제출 존재만 확인하고 안정화된 출력 및 고정된 예열 제출 수를 증명하지 않는다. SDK 지터/초기 히스토리·baseline 안정화와 첫 이동 잔상의 인과관계는 추가 조사 대상이며, 이번 결과를 steady-state 전체 화질 수용으로 확대하지 않는다.

통과 37조합의 프레임별 최대 평균/p95는 HDR 0.032301/0.116891, final display 0.094861/0.228572다. 최종 판정기로 기존 Quality·확대 FSR/XeSS Ultra Performance의 13변이씩 총 39개를 모두 거부하고 원본 증거 유지도 확인했다. 39조합의 최종 global basic DX12 receipt는 메시지/문제/누락 0이며, Debug XeSS Ultra Performance의 최초 실패 실행은 최종 global receipt가 없어 미확인으로 남긴다(640개 캡처의 validationCount는 0). 모든 실행 프로세스는 종료됐고 실행 바이너리는 이전 SDK/native/golden 검증과 동일한 Debug/Release runtime hash다. 이번 수정은 검증 도구·문서 범위이며 엔진 재빌드를 수반하지 않는다. Debug FSR Quality의 최초 `request.deadline` 조회 실패는 보존하고 POST의 새 연결 및 async polling을 사용한 새 실행에서 전체 통과했다.

정본은 `Artifacts/PR169/sdk-quality-matrix/results.json`이다(`completed=true`, `passed=false`, 37통과/3실패). 각 구성/provider/quality/host-size 디렉터리에 실행·fixture·oracle·복원 상태·프로세스 종료를 보존한다. Debug FSR Quality만 `-retry`가 최종 수용 증거이며 최초 실패는 원본 디렉터리에 유지한다. Debug XeSS Ultra Performance의 전체 16프레임 재판정은 `verification/reaudit.json`, 최종 39변이 감사는 `mutation-reaudit.json`이다. 다음 우선 작업은 위 XeSS 최초 이동의 초기 히스토리·baseline 출력 증명을 보완하고 같은 기준으로 원인을 검증하는 것이다. silhouette/다른 재질·카메라 연속 이동·variable-time SDK·물리 pacing/성능 등은 여전히 잔여다.

FSR/XeSS Quality의 원근 카메라 연속 승인 제출과 opaque quad 내부/disocclusion 픽셀을 후속 수용했다. 회색 배경 앞 붉은 quad를 0.5 world-unit씩 8프레임 이동시키고, 실제 component disable 이후 8프레임 회복을 관측했다. SDK native exclusion 없이 고정 SDK packet 1000/60ms를 사용하며 측정 step은 각각 한 번만 제출한다. DX12 Debug/Release × FSR/XeSS 4조합, 총 64 GPU 캡처·60개의 캡처 간 exact source/real predecessor 연결을 통과했다. 최종 display는 332x144, 실제 렌더 크기는 FSR 221x96·XeSS 196x85다. authored 입력/asset hash, 같은 view/scene/history, non-reset actual SDK provenance, 실제 foreground/background color·depth 및 current-to-previous render-pixel 모션 11,127 probe를 확인했다. 배경 노출/회복 영역 31,680픽셀에서 HDR은 같은 프레임의 SDK 전 입력과, 최종 GPU display는 같은 프레임의 안정 배경 영역과 비교한다.

측정 전에 정한 제한은 foreground/background contrast 대비 residual 평균 ≤0.10·p95 ≤0.25, 최소 contrast 0.03, 경계에서 render-pixel 2개 제외다. 네 조합의 프레임별 최대 평균/p95는 HDR 0.0165383/0.0316578, final GPU display 0.0557166/0.0869566으로 모두 통과했다. 각 조합의 provider/clock/reset/predecessor/session 및 상수 HDR/display·0 모션·잘못된 depth/color·NaN·잘린 파일 12변이를 모두 거부했다(총 48개). 모든 SDK 캡처의 basic DX12 validation은 0이며, 원래 native 7-route의 각 14캡처·63 probe, 일반 golden 캡처·SSGI 취소/reset 및 히스토리 계약 각 494단정도 Debug/Release에서 통과했다. 현재 binary hash는 해당 구성의 SDK/native/golden 실행과 일치하고 테스트 process 종료를 별도 확인했다.

정본은 `Artifacts/PR169/sdk-sequence/results.json`, 실행은 `accepted-{fsr,xess}-{Debug,Release}/verification`, 변이 재감사는 각 `mutations/reaudit.json`, 회귀는 `golden-{Debug,Release}`와 `Artifacts/PR169/motion/sequence-native-{Debug,Release}`다. 도구는 `Verify-TemporalSdkSequence.ps1`·`Compare-TemporalSdkSequence.py`·`TemporalSdkSequenceOracleTests.py`이며, `render.temporal.motion.fixture ... sdk-sequence`가 기존 typed asset/scene lifecycle을 통해 장면을 일시 격리·복원한다. SDK 진단의 provenance는 native golden과 분리하며 폴백/FG/NIS/DeepDVC/fault 결과를 수용하지 않는다. 최초 실행의 서비스 5초 sync timeout은 보존했고 async operation polling으로 driver를 수정했다. 이 수용은 위 단순 opaque quad의 내부와 새로 드러난 배경에 한정한다. silhouette 경계, skinned/alpha/foliage 등 다른 재질 경로, 다른 품질/해상도·variable-time SDK 및 물리 scanout/pacing/성능은 잔여다.

연속 SDK 화질 검사의 선행인 진단 측정 프레임 연결을 보완했다. 기존 native 7-route fixture는 readback 대기 중 같은 측정 자세를 여러 번 제출했다. 보존한 Debug 기록의 static current real-frame 1071 다음 control의 실제 predecessor는 1075였으며, 강화한 검사는 이를 거부한다. tagged Game 진단에서 같은 session/view/scene/source history의 이미 제출된 측정 자세와 더 오래된 queued 자세를 `PrepareTemporalView` 전에 제외한다. baseline은 non-reset history를 확보하도록 반복할 수 있고 일반 untagged 렌더 및 새 신원은 기존 admission을 유지한다. 새 fixture의 `singleSubmissionPerMeasuredStep` 선언은 JSON emitter에서 1로 저장되며 새 실행 도구는 선언 누락을 거부한다. 독립 oracle은 다음 control의 실제 predecessor real-frame이 current 캡처 real-frame과 정확히 같아야 한다.

SDK 활성 DX12 Debug/Release 빌드 후 각 7개 경로·14 GPU 캡처·63 probe가 통과했고 두 구성의 14개 measured/control 연결이 모두 정확히 일치한다. 각 구성의 basic validation 합계는 0, 기존 golden 캡처·SSGI 취소/reset 회귀·히스토리 계약 485단정도 통과했다. oracle 단위 회귀 23개와 실제 Debug 캡처의 predecessor 신원 변이 거부를 확인했다. 정본은 `Artifacts/PR169/motion-step/results.json`, 실행은 `Artifacts/PR169/motion/step-{Debug,Release}`이며 런타임 hash는 해당 golden 회귀 및 현재 바이너리와 일치한다. 이 수용은 native 진단의 제출 순서 보존이다. 현재 fixture의 직교 카메라는 SDK 검사에 사용할 수 없으므로 원근 카메라·배경 가림을 포함한 연속 FSR/XeSS ghosting/disocclusion 픽셀 검사는 여전히 잔여다.

FSR/XeSS의 실제 장면 SDK 회복 픽셀을 DX12 Debug/Release에서 후속 수용했다. 큐브·바닥·광원 live 장면에서 실제 카메라 이동·cut, host resize 960x640·1280x720 이후 첫 번째·여덟 번째 승인 제출을 캡처했다. 두 SDK × Native AA/Quality/Balanced/Performance/Ultra Performance × 두 구성의 20조합, 총 260캡처·120 recovery/reference 비교에서 SSGI RGBA·재구성 HDR·최종 GPU display 최대 오차가 모두 0이고 basic DX12 validation 메시지·문제·누락은 0건이다. 기존 진단 readback의 표시/렌더 크기 혼용을 `frameContext` 실제 크기로 수정했고, SDK 출력은 display extent에서 읽는다. 모든 조합에서 실제 카메라 이동의 depth 및 HDR/display RGB 변화와 비어 있지 않은 출력도 독립 확인했다. 각 조합의 기존 17변이 거부와 두 SDK/두 구성 Quality의 상수 HDR/display 8변이 추가 거부를 기록했다. 기존 golden 캡처·temporal 계약 468단정·SSGI 취소/reset 회귀도 두 구성에서 통과했다.

이 SDK 비교는 명시적 `fixed-sdk-clock`으로 대상 SDK packet의 시간 간격만 1000/60ms로 맞춘 조건이다. 카메라·시뮬레이션 시계는 live이며 캡처는 native exclusion 없이 실제 SDK를 실행하고 golden 수용에서 제외된다. FSR의 서로 다른 variable-time 실행은 HDR 최대 오차 0.14794921875로 엄격 비교에 실패했으므로 별도 보존하며 허용오차를 늘리지 않았다. 정본은 `Artifacts/PR169/sdk-pixels/results.json`, 실행은 `verified-{fsr,xess}-{Debug,Release}-{quality}`, 최종 출력 검사와 회귀는 `final-oracle.json`·`golden-{Debug,Release}`다. 도구는 기존 `Verify-LiveSSGIRecovery.ps1 -Provider fsr|xess -Quality ...`를 확장했다. 수용 범위는 이 정적 장면/명시적 SDK 시간 조건의 cut·resize 회복이며, 연속 이동의 잔상·disocclusion 화질, variable-time 화질, native 기존 픽셀 동등성 및 물리 scanout은 잔여다.

SSGI의 native live camera-cut/실제 창 resize 회복 픽셀을 DX12 Debug/Release에서 후속 수용했다. 정상 Editor의 glTF 큐브·바닥·광원 장면에서 실제 카메라 이동과 cut, host resize 960x640·1280x720을 실행하고 다음 reset의 첫 번째·여덟 번째 승인 제출을 관측 캡처했다. 각 구성 13캡처·6 recovery/reference 쌍에서 SSGI RGBA 및 최종 GPU display 이미지 최대 오차가 모두 0이고 유효 표면 누적은 1→8이다. 카메라 이동의 깊이 픽셀 최대 차이 0.034343183은 실제 pose 변화가 렌더에 도달했음을 확인한다. 기존 SSAO 전역 회전 카운터 때문에 같은 reset 순서의 조명·SSGI 입력이 달라지는 실패를 재현하고, 회전을 뷰의 제출 승인된 sample sequence에 연결해 수정했다. 캡처는 자체 히스토리를 초기화하지 않고 reset·cut·view/scene·승인 predecessor 신원을 기록하며 일반 golden 캡처의 native exclusion/controlled 동작은 유지한다. 별도 commandlet 회귀에서 두 구성 모두 기존 golden 캡처, temporal 계약 468단정, SSGI 취소 3/reset 8비교 및 basic DX12 validation 0건을 통과했다. 정상 13캡처 수용과 신원/NaN/유한 픽셀 손상 변이 8개의 거부도 실행했다. 정본은 `Artifacts/PR169/ssgi-live/results.json`, live 통과 기록은 `accepted-{debug,release}`, golden 회귀는 `golden-ready-{Debug,Release}`에 둔다. 도구는 `Verify-LiveSSGIRecovery.ps1`·`Compare-LiveSSGIRecovery.py`다. 수용 범위는 native 최종 GPU 이미지까지이며 실제 scanout, SDK 업스케일 화질 및 native 기존 픽셀 동등성을 대신하지 않는다.

SSGI reset 소비 경계의 GPU 픽셀 복구를 후속 수용했다. 8프레임 누적한 pass를 새 pass와 비교해 같은 pose의 공용 camera-cut reset, 축소/확대 resize reset, 공용 reset 없이 해상도만 바뀐 재생성의 4경우에서 첫 프레임과 8프레임 후를 검사했다. 실제 크기의 절차적 depth/normal 입력은 각각 256x256·128x192·384x320·192x128이며 RGBA 전체를 비교한다. DX12 Debug/Release 각 8비교 모두 최대 오차 0, 첫 프레임 유효 표면 누적 1·8프레임 후 평균 8, 기존 취소 복구 3경우 통과 및 basic validation 메시지·문제·누락 0건이다. 해상도만 바뀐 경로는 수정 전 첫 프레임 최대 오차 0.971679688로 실패했다. 텍스처 재생성 완료 시 `ResetHistory()`로 샘플 순서·이전 카메라·슬롯도 초기화해 수정했다. 로컬 정본은 `Artifacts/PR169/ssgi-reset/results.json`, 실패 재현은 `before-debug`, 통과 실행은 `verified-{debug,release}`에 보존한다. 같은 재실행 도구가 취소 3비교와 reset/재생성 8비교를 모두 요구한다. reset 신호는 검사가 `PrepareFrame`에 직접 전달하므로 live 이동 카메라·실제 창 변경부터 최종 화면까지의 픽셀 수용, device loss, 공용 view history 흡수 및 SDK 업스케일 화질은 잔여다.

SSGI 후속 검증은 준비·그래프 선언·명령 기록 후 제출 전 취소의 3지점을 DX12 Debug/Release에서 통과했다. 샘플 순서·히스토리 슬롯·이전 카메라는 제출 승인 시에만 갱신하며, 취소는 마지막 정상 히스토리를 보존한다. 구/바닥이 있는 절차적 장면에서 각 취소를 8회 끼워 넣은 실행을 정상 8프레임 실행과 비교했고, 각 구성의 3경우 모두 RGBA 65,536개 값의 최대 오차가 0이었다. 기존 누적은 유효 픽셀 9,032개에서 평균/최소/최대 8, basic DX12 validation 메시지·문제·누락은 0건이다. 로컬 정본은 `Artifacts/PR169/ssgi/verified-{debug,release}/results.json`, 재실행 도구는 `Tools/temporal-validation/Verify-SSGIAbortRecovery.ps1`다. 이 실행의 수용 범위는 SSGI 제출 전 취소 복구이며 reset 소비 경계의 픽셀 복구는 위 후속 실행이 보완한다.

후속 live history recovery 검증은 native/FSR/XeSS 각각 일반 이동·camera cut·두 차례 실제 창 resize·연속 프레임 복구의 5항목을 DX12 Debug/Release에서 실행해 각 15항목을 통과했다. 이동은 제출된 카메라 위치가 실제로 0→0.1에 도달한 프레임에서 history revision 유지로 확인한다. cut/resize는 제출된 reset의 신원·view/scene·해상도·첫 Halton jitter를 보존하고 해당 reset frame의 GPU 완료를 기다린다. basic DX12 validation은 문제·메시지·누락 0건이며 원래 host client 크기를 복원했다. 순수 reset predicate는 정상 live reset 9개를 수용하고 신원/jitter/해상도/view/scene/provider 변이 9개를 거부했다. 계약 회귀는 latch의 취소/유지 단정을 포함해 각 구성 468개를 통과했다. 로컬 정본은 `Artifacts/PR169/recovery/results.json`, 재실행 도구는 `Tools/temporal-validation/Verify-TemporalHistoryRecovery.ps1`다.

이 과정에서 `window.resize`가 자체 editor chrome에 기본 OS caption 높이를 중복 적용해 실제 client 높이를 45px 크게 만드는 결함을 수정했다. 실제 outer/client 차이로 계산하며 `window.info`는 기존 screen-bus 크기와 host client 크기를 별도로 보고한다. 위 복구 증거는 camera-only scene의 렌더 제출·SDK dispatch·GPU 완료 관측이다. 회복 픽셀, native 기존 픽셀 동등성, 비어 있지 않은 장면의 업스케일 화질 및 SSGI 취소 복구 완료로 확대하지 않는다.

아래 10월 9일의 미실행 기록은 당시 상태다. 현재 작업 checkout에서는 SDK 활성 DX12 Debug/Release 빌드와 TR1 7-route 모션 픽셀 검증을 실행했다. 로컬 증거는 `Artifacts/PR169/motion/results.json`에 있으며, 각 구성 14 GPU 캡처·63 probe와 strict oracle을 통과했다. 이 결과를 전체 16행 완료로 확대하지 않는다.

`dx12.temporalhistory`는 실제 `EnhancedTemporalViewState::Begin/Commit`과 DX12 디바이스·FSR/XeSS context configure를 검사한다. Debug/Release 각각 407개 단정과 basic DX12 validation 0건을 통과했다. 독립 Halton 샘플, render-pixel jitter 행렬, 취소 시 시퀀스 유지, 일반 이동, cut/FOV/명시적 reset/resize/view/scene/source revision/품질 변경 및 native-AA 1:1을 확인했다. Commit은 검사가 직접 호출하므로 live camera-cut/resize 제출 또는 SDK 픽셀 수용 증거가 아니다. 로컬 결과는 `Artifacts/PR169/history/accepted-{debug,release}/contract.json`에 보존한다.

같은 바이너리의 live Editor camera scene에서 FSR/XeSS quality·native-AA dispatch와 명시적 history reset 관측을 각각 확인했다. reset driver는 정확한 요청 세대의 새 제출 프레임과 history revision 증가를 요구한다. 빈 camera scene의 관측이므로 업스케일 화질을 판정하지 않는다. TR2의 native 기존 픽셀 동등성, live resize/camera-cut 회복 픽셀, 비어 있지 않은 장면의 시간축 품질 검증은 남는다. TR3의 SSGI는 공용 reset 신호를 소비하지만 자체 radiance/depth ping-pong 및 재투영 행렬을 유지한다. 자체 히스토리 흡수는 잔여이며 제출 전 취소 복구는 위 후속 GPU 검증으로 보완했다.

## 2026-10-09 제품 설정·히스토리·profiler 소스 보완

Editor `editorRenderFeatures` / Player `build.renderFeatures`의 portable 기본값과 cook 후 `renderFeatures`·device 생성 전 소비, 저장값→명시적 환경 override→세션 CLI 우선순위, 요청/지원/설정/활성 MFG count 분리를 소스로 연결했다. 명시적 camera cut·unjittered projection 변경은 마지막 제출 history와 비교한다. CPU profiler frame owner와 render publication/real-frame/submission 신원을 분리하고, CPU render-submit/GPU-pass provenance sidecar를 `.ceprof` snapshot v6·stream v7 (기존 v4/v5 읽기 유지) 및 viewer/summary로 운반한다. live PLRD는 v3이며 legacy 기록의 provenance는 unknown으로 남긴다. development-only TU/FG fault와 결함 활성 측정의 fail-closed, CPU presenter-return 연속 기록도 소스 범위다. QPC 반환 관측은 물리 표시 시각이 아니다.

Presenter observation은 정확한 source identity를 가진 CPU Present 결과이며 물리 display timestamp·개별 generated identity·GPU/SDK 최종 소비 증거가 아니다. 외부 latency/pacing 수집·분석 소스는 작성되었으나 실행 수용은 전부 남는다. development-only 전체 7-route 씬 생성/제출 sequencing·probe pixel 소유권 driver와 strict v2 oracle을 소스로 작성했다. 실제 자산·제출 predecessor·입력 hash·positive/disabled-control GPU 픽셀·indexed/meshlet 실행 경로가 모두 일치해야 향후 PASS할 수 있다. 기존 analytic fixture 및 증거 누락은 여전히 INCOMPLETE다. 새 driver·checker도 실행하지 않았으며 TR1 수용 완료를 뜻하지 않는다.

FG capability/configure/prepare/Present 및 실제 drain 이후 host acknowledgement fault, FSR 전용 evaluate callback fault/staging은 소스로 작성했다. DLSS/XeSS의 별도 evaluate 주입은 명시적으로 미지원이며 실제 device-loss·소비 지연·물리 표시 행렬 수용을 대신하지 않는다. 상세 범위는 [통합 기록](TemporalVendorIntegration20261009.md#product-defaults-history-and-profiler-gap-closure--source-only-checkpoint)에 둔다. W9는 temporal/render-feature 설정 부분만 보완했으며 전체 설정/품질/제품 수용 완료가 아니다. 빌드·테스트·스크립트·native 실행 없이 정적 검토만 했고 **16행/71일·기성 0 및 모든 DX12 Debug/Release 실행 수용 미완료**를 유지한다.

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
| `TR0` | 계측 무해화 계약 — 모든 측정 표면의 프레임 종류 선언 | 진행 | BASE-0 | 3 |
| `TR1` | 모션 벡터 생산 — static/skinned/instanced/decal/알파 | 완료 | BASE-0 | 8 |
| `TR2` | 렌더 해상도 ≠ 표시 해상도 계약·jitter 시퀀스 | 진행 | TR1 | 5 |
| `TR3` | 히스토리·이전 프레임 행렬 공용 계약, SSGI 자체 히스토리 흡수 판정 | 진행 | TR2 | 5 |

`TR0`가 트랙 첫 항목인 것은 실수가 아니다. §7을 참조한다.

### 5.3 트랙 TU — 업스케일, 20인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TU0` | 벤더 중립 Upscaler 인터페이스·런타임 기능 질의·폴백 사슬·DirectSR 판정 | 진행 | TR3 | 3 |
| `TU1` | 무-upscale 기준 경로 — 폴백 마지막 칸이자 A/B 대조군 | 진행 | TU0 | 2 |
| `TU2` | FSR 백엔드 — 벤더 중립 베이스라인 | 진행 | TU0 | 5 |
| `TU3` | DLSS Super Resolution 백엔드 (NGX) | 진행 | TU2 | 3 |
| `TU4` | XeSS 백엔드 | 진행 | TU2 | 3 |
| `TU5` | PostChain 앞 삽입·슬롯 schema 크기 유도·UI 합성 위치 재확인 | 완료 | TU2 | 4 |

`TU3`·`TU4`가 `TU2` 뒤에 오는 것은 의존이 아니라 **순서 규약**이다. 벤더 중립 경로가 먼저
서야 벤더 백엔드가 "추가 경로"가 되고, 그 반대면 벤더 백엔드가 기준이 되어 폴백이 2등 시민이
된다.

### 5.4 트랙 FG — 프레임 생성, 26인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `FG0` | present 소유권 이전·proxy swapchain·frame pacing 계약 | 진행 | TU5 | 8 |
| `FG1` | 지연 마커 — Reflex·Anti-Lag 2, 게임 루프 경유 | 진행 | FG0 | 4 |
| `FG2` | FSR 3 Frame Generation 백엔드 — 벤더 중립 베이스라인 | 진행 | FG0 | 6 |
| `FG3` | DLSS Frame Generation 백엔드 | 진행 | FG2 | 4 |
| `FG4` | HUD-less 백버퍼·UI 합성·**에디터 금지 강제** | 진행 | FG2 | 4 |

`FG1`은 렌더 트랙 안에서 끝나지 않는다. 지연 마커는 시뮬레이션 루프에 박혀야 하므로
3계층 분리(L0~L4) 경계를 가로지른다. **이 항목만 다른 계층의 승인을 받는다.**

### 5.5 통합 게이트 — 4인일

| ID | 내용 | 상태 | 선행 | 일 |
|---|---|---|---|---:|
| `TFG9` | 지원 행렬 전수·폴백 사슬·계측 단정·성능/지연 판정 | 진행 | TU3, TU4, FG3, FG4 | 4 |

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
