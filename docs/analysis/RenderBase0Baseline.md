# BASE-0 구현과 검증 기록

2026-10-04 RG5-12 화면 SSS/SSR 이관 이후 현행 기준선은 [화면 소비 체인 검증](RenderRg5ScreenMigration.md)을 따른다. Debug/Release GPU 각 3정책·48 frames·정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0·현행 소스/바이너리 SHA-256 및 phaseComplete=true를 확인했다. RG5 전체와 전체 제품 SceneRenderer의 versioned GPU 수용/RG6 기본 전환은 열려 있다. 아래 과거 증거는 보존한다.

## 현재 완료 계약 — 2026-10-02 범위 정리

BASE-0은 **RenderGraph 구조 변경 전 DX12 기준선**이다. 범용 frame packet 직렬화나 모든 재질·애니메이션
재생의 완성은 RG1 착수 조건이 아니다. 아래 기존 슬라이스의 `complete=false`·범용 재생 잔여 표기는 당시
확장된 계약의 역사적 판정이며, 현재 완료 조건은 이 절을 따른다. 과거 artifact는 수정하지 않는다.

완료 조건은 다음 여섯 가지다.

1. **현재 소스와 바이너리:** 같은 소스로 빌드한 Debug/Release Editor를 사용하고 executable/runtime DLL·소스
   SHA-256을 기록한다. 실행 중 변경이 없어야 하며 완료 판정 시에도 현재 파일과 일치해야 한다.
2. **고정된 대표 장면:** 현행 artifact/host identity로 재쿠킹한 동일 자산·장면·카메라·해상도·tuning·환경을 사용한다.
   IBL 1024/4096과 기존 이미지 상한을 유지한다. 파일 기반 범용 재생 대신 고정 자산/설정 재구성을 허용한다.
3. **이미지 기준선:** 각 구성에서 독립 DX12 프로세스 2개 × 캡처 2개를 비교하고 Debug/Release도 대조한다.
   기본 첨부와 캡처된 중간 HDR의 선형 오차·RMSE·차영상·PNG를 보존하고 기존 오차 상한 안에서 통과해야 한다.
   과거 16개 이미지의 bit-exact는 관측 결과이며 현재 바이너리의 재검증을 대신하지 않는다.
4. **graph 진단과 변이:** 현행 declaration-order/inferred-state 계약의 pass 순서·사용·컬링·수명·Transition/UAV
   진단과 native graph fixture를 보존한다. producer/order/edge/lifetime/barrier/input/pixel 변이가 실제 거부되어야 한다.
   BASE-0이 의존성 DAG 또는 version/Modify를 구현했다고 주장하지 않는다.
5. **계측과 실행 건전성:** capture submission의 CPU compile/record·pass별 GPU 시간·VRAM과 측정 scope를 기록한다.
   timestamp 누락·비유한 픽셀·GPU validation·encoder drop·입력/소스 변경·비정상 종료·강제 종료가 없어야 한다.
   일반 FPS 또는 MAT-9 성능 합격의 수치 기준을 새로 만들지 않는다.
6. **두 구성의 최종 회수:** 위 조건이 두 구성에서 모두 통과하고 해당 첫 캡처끼리 비교해야 BASE-0 전체 완료다.
   `complete-render-base0.ps1`이 현재 바이너리/소스 및 결과의 연결을 확인해 별도 `phaseComplete=true` 증거를 쓴다.

기본 `verify-render-base0.ps1`의 `complete`는 **한 구성의 기준선 통과**다.
`genericPacketReplayVerified`·재질/포즈 재생 여부는 완료 식에 포함하지 않는다.
`-ReplayExtensions`는 추가 진단 실행이며 native `dx12.rendergraph replay-extensions`,
제품 재생 캡처와 파일 재생 검사를 명시적으로 선택한다. 기존 재생 코드·기록은 보존한다.

2026-10-03 독립 선택 경계: `controlled-replay`는 draw 입력 저장만 선택하며 카메라 파일만 지정하면
draw/Lattice 저장 검사도 실행하지 않는다. `controlled-lattice-replay` 또는 명시적인 Lattice 입력 파일만
Lattice 밀봉·재생을 활성화한다. 재질의 정상 렌더링 경로는 항상 유지한다. manifest는 Lattice 확장의
선택·실행 여부를 기록하며 미선택 캡처에 Lattice 계약을 선언하지 않는다.
`-ReplayExtensions`는 `baseline/`과 `replay-extension/`에서 독립 실행한다. 최상위 결과는 기본 기준선의
판정을 유지하고 확장 결과·오류는 별도 필드로 참조한다. 확장 전용 결과는 최종 완료 도구가 거부한다.

2026-10-03 분리 검증: VS18/v145 Debug Editor 빌드와 실제 DX12 캡처의 기본·카메라·draw 저장·draw 재생
네 모드에서 Lattice 미선택·미실행 및 파일 부재를 확인했다. 명시 선택한 Lattice 저장과 파일 재생도
현재 fixture의 draw 1개에서 성공했다. 이후 일반 캡처와 이전 일반 캡처의 16개 이미지가 maxError=0,
validationCount=0, GPU 측정 완료, 정상 종료 0이었다. 증거는
`Build/Obj/Base0IndependentReplay/Debug-live-v2/{result.json,responses.jsonl,post-extension-isolation/comparison.json}`.
확장 실패·native 종료 코드 주입은 기본 판정과 호출자 종료 코드를 오염시키지 않았고, 확장 전용 결과의
최종 완료 제출은 거부됐다. 이는 분리 경계의 Debug 검증이며 범용 재질 재생이나 BASE-0 최종 회수가 아니다.

**2026-10-03 현재 상태는 done·완료 공수 4인일이다.** 현행 Debug/Release에서 각각 독립 프로세스 2개 ×
캡처 2개를 수집했다. 구성 내 재현·graph/변이·계측·GPU validation·정상 종료를 통과했고 구성 간 자산·환경·
tuning 신원도 일치했다. 구성 간 16개 이미지의 maxError=0, GPU 측정 완료, 현재 소스·각 바이너리 해시
일치를 최종 도구가 검증하여 `phaseComplete=true`를 기록했다. 재생 확장은 모두 비활성화했다.
최종 증거는 `Build/Obj/RenderBase0/{Debug-current-static-v4,Release-current-static-v4}/result.json`,
`Debug-Release-current-static-v4/comparison.json`, `current-static-v4-phase-complete.json`이다.
초기 `Debug-current-static-v2`는 cold import가 모델 `.meta` generation을 갱신해 입력 고정 검사에서 거부됐다.
원본 자산은 준비 중에도 불변으로 검사하고, 준비 완료 후 `.meta`까지 고정하도록 경계를 수정했다.
`Debug-current-static-v3`는 빌드와 겹친 진단 기록이며, 최종 v4는 빌드 종료 후 두 구성을 순서대로 수집했다.
이전 재생 결과와 실패 기록은 보존한다. MAT-9 성능 판정과 구분하기 위해 CPU/GPU 시간은 readback을 포함한
캡처 제출 범위, VRAM은 device budget snapshot으로 해석한다. RG1 스케줄링 구현은 아직 미착수다.

분리한 후속 범위: 범용 재질/program/texture·실제 animated/skinned pose·광원/tuning/history 파일 재생.
MAT-9 SSS/투과 품질·실제 성능 수용은 그대로 열어 두며 Vulkan RenderDoc/리소스/픽셀 비교는 PHASE 4.9만 소유한다.

## 과거 구현·검증 이력 — 이하 당시 판정

> **2026-10-01 사용자 지시로 완료 범위 정정.** BASE-0은 DX12 Debug/Release의 입력 재현·graph/계측·회귀 검증이다. 아래 Vulkan 비교 실행/오류/수정은 과거 관측 기록이며 현재 BASE-0의 선행·잔여·실패 조건이 아니다. 잔여 SSGI/HDR/display 교차 오차와 Vulkan 비교는 [PHASE 4.9](../plans/BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교로 인계했다. 당시 result.json은 당시 잘못 설정된 범위의 결과로 보존하며 덮어쓰지 않는다. 범용 sealed packet replay 등 BASE-0 고유 잔여는 별도다.


2026-10-02. 상태: **진행 중**. MAT-9·RG1~RG9 완료를 뜻하지 않는다.

## 카메라·렌더 시간 파일 재생 — BASE-0 첫 재생 슬라이스

2026-10-01 `camera-clock-v1`을 제품 `render.live.capture`에 연결했다. 정지 자산을
다시 읽는 기존 하네스 위에서, 저장한 카메라/시간 값이 실제 제품 렌더 입력으로 재주입되는지 검증한다.

- `camera-input.bin`은 368바이트 little-endian 형식이다. magic/version, 해상도·표시 대상·view flags·배경 상태,
  total/delta, 직교 여부, 카메라 행렬 4개·벡터 4개·FOV/near/far와 checksum을 담는다.
  포인터·구조체 padding·frame ID·scene epoch·자원 handle을 파일로 저장하지 않는다.
- 기존 명령의 선택 인자로 `controlled <camera-input-absolute-path>`를 받는다. 파일을 먼저 읽고 검증한
  값만 요청이 소유한다. RT는 해당 캡처의 지역 frame 사본에 카메라·시간만 적용하며,
  발행 순서·scene epoch·resize generation과 자원 수명은 현재 프로세스의 계약을 유지한다.
- 파일 손상/버전/비유한 값/잘못된 target은 출력 폴더 생성 전에 거부한다. 실제 frame의 크기·view flags·
  배경 상태가 다르면 캡처만 실패시키고 일반 live 입력으로 진행한다. 실패한 decode는 이전 정상 값을 보존한다.
- 기본 정지 캡처는 시간 0·SSAO sample 0·SSGI/fog 재시작을 유지한다. 0이 아닌 시간을 재생한 진단은
  `captureMode=camera-clock-replay-v1`로 구분하며 정지 기준선 이미지 수용에 섞지 않는다.
- 메시·재질·본 포즈·광원·셰이더/자산 owner·tuning·시간축 history를 이 파일이 저장한다고 주장하지 않는다.
  이 값들은 아직 현재 장면에서 공급된다. `cameraClockReplayVerified`와 `genericPacketReplayVerified`를
  별도로 보고하며, 후자는 아직 false다. **BASE-0 전체 완료와 RG1 착수 통과를 올리지 않는다.**

검증 현황: VS18/v145 Debug/Release Editor 빌드·각 네이티브 roundtrip/11종 거부·제품 파일 6종 거부를 통과했다.
실제 카메라 이동 재생은 GBuffer baseColor 156성분을 바꿨고, 이후 일반 캡처는 원본의 7개 첨부와
bit-exact였다. 시간 17.25초/delta 1/60 주입과 파일 바이트 보존도 확인했다.
Debug/Release 각각 독립 DX12 프로세스 2개 × 정지 캡처 2개 모두 이미지 7개 bit-exact,
GPU validation 문제 0, sourceChanges=0, 강제 종료 없이 exit 0이다. 각 구성에서 cameraClockReplayVerified=true,
genericPacketReplayVerified=false, complete=false이며 실패 항목은 없다.
기본 첨부 7개와 중간 HDR 9개를 합친 16개 이미지 모두 동일/독립 프로세스 및 Debug/Release 간 bit-exact다.
구성 간 대조 증거는 `Build/Obj/RenderBase0/camera-replay-Debug-Release-v1/comparison.json`이다.

증거: `Build/Obj/RenderBase0/Debug-camera-replay-v1/result.json`, `Build/Obj/RenderBase0/Release-camera-replay-v1/result.json`,
`dx12-0/shifted-camera/camera-replay-verification.json`, `dx12-0/replay-isolation/comparison.json`,
`dx12-process-repeatability/comparison.json`. 하네스는 DX12 기본값을 유지하며 Vulkan 비교는 실행하지 않았다.

이후 선택된 draw의 transform/pose 슬라이스를 아래처럼 추가했다. 재질 typed 값과 범용 입력 재생은
여전히 잔여이며 이 카메라 슬라이스의 성공으로 대신하지 않는다.

## 선택된 draw의 transform·pose 파일 재생 — BASE-0 두 번째 슬라이스

2026-10-02 `selected-transform-pose-v1`을 제품 `render.live.capture`에 연결했다.

- `draw-input.bin`은 `CEDRW001` magic/version, draw 수, 각 draw의 route(opaque/forward/LX),
  ModelId/MeshId 32바이트, 지오메트리 digest, bone 수, 월드 행렬과 본 행렬, 전체 checksum을
  little-endian으로 저장한다. 구조체 padding·포인터·프로세스의 generation/frame/animator key는 저장하지 않는다.
- 입력은 최대 64MiB, draw 16,384개, draw당 bone 1,024개로 제한한다. 실제 남은 payload 크기도 검사하고
  비유한 행렬·버전·route·과대 count·잘린 파일·checksum 오류는 요청 단계에서 출력 폴더 생성 전에 거부한다.
  decode 실패 시 기존 정상 값은 유지한다.
- 명령은 `render.live.capture <new-absolute-directory> editor controlled <camera-input-path> <draw-input-path>`다.
  Editor/Game의 controlled camera 재생이 선행 조건이며 MaterialPreview는 이 draw 재생 계약에 포함하지 않는다.
- 현재 카메라가 선택한 draw 목록에서 자산 ID, vertex/index 실제 바이트와 layout/stride/count의 digest,
  route와 pose count를 **전체 대조한 뒤** 월드/포즈를 적용한다. 불일치 시 진단 캡처만 실패하고 일반 live draw는
  부분 변경하지 않는다. geometry/program/texture는 현재 프로세스의 immutable owner와 수명 계약을 소비한다.
  파일이 메시 payload 자체나 재질/program/texture owner를 복원하는 범용 자산 archive라는 뜻은 아니다.
- 재생 포즈는 캡처 요청의 vector가 소유하고 GPU 준비가 끝날 때까지 유지한다. LX scene sealing 전 적용하며,
  LX fallback에도 같은 월드/포즈를 전달한다. 재생용 animator key는 현재 선택 목록에서 새로 만들고
  draw별로 구분해 서로 다른 저장 포즈가 기존 dedup key를 공유하지 않도록 한다.
- 재생은 이미 선택된 목록에 적용한다. 사라진 자산·다른 선택 목록을 복원하거나 저장 draw로 절두체 선택을
  다시 계산하지 않는다. 재질 값·광원·tuning·시간축 history 재생도 이번 파일의 범위가 아니다.

검증: VS18/v145 Debug/Release Editor 빌드 성공. 각 네이티브 fixture는 roundtrip, 파일 거부 10종,
자산/실제 geometry/pose count 불일치 3종, 원자적 적용, producer 포즈 저장소 변경 후 소유 수명,
opaque/forward/LX와 LX fallback 적용을 확인했다. 각 제품 실행은 잘못된 파일/closure 9종을 거부했고,
월드 x 이동 0.25 재생으로 실제 GBuffer baseColor 156성분이 바뀌었다. 파일 바이트도 그대로 보존했다.

각 구성의 독립 DX12 프로세스 2개 × 기준선 캡처 2개에서 첫 캡처의 camera/draw 파일을 나머지 캡처에
재주입했다. 동일/독립 프로세스 및 Debug/Release 간 기본 첨부 7개 + 중간 HDR 9개, 총 16개 이미지가
bit-exact(maxError=0)다. 재생 이후 일반 캡처도 원본과 일치한다. GPU validation/encoder 문제 0,
sourceChanges=0, 모든 프로세스 exit 0·강제 종료 없음이다. 병행 빌드 중 관측 시간을 성능 합격 근거로 사용하지 않는다.

정적 LX 장면에는 bone이 없다. 따라서 `selectedTransformReplayVerified=true`,
`cameraClockReplayVerified=true`지만 `animatedLivePoseReplayVerified=false`,
`genericPacketReplayVerified=false`, `complete=false`다. **본 소유 수명의 네이티브 검사를 실제 스키닝 픽셀
재생 검증으로 대신하지 않는다. BASE-0 progress·완료 공수 0, RG1 not-started를 유지한다.**

증거: `Build/Obj/RenderBase0/{Debug,Release}-draw-replay-v1/result.json`, 각 `dx12-0/graph-fixtures.results.jsonl`,
`dx12-0/shifted-world/draw-replay-verification.json`, `dx12-0/replay-isolation/comparison.json`,
`dx12-process-repeatability/comparison.json`, `Build/Obj/RenderBase0/draw-replay-Debug-Release-v1/comparison.json`.
Vulkan 비교는 실행하지 않았으며 PHASE 4.9 범위를 유지한다. IBL 1024/4096·기존 오차 상한·MAT-9 조건도 유지한다.

다음: ShaderMeta/Forward/Lattice 재질 typed 값과 program/texture identity·owner의 밀봉/재생,
실제 animated/skinned draw 재생의 픽셀/스키닝 검증, 나머지 광원·tuning·history 등 범용 packet 범위 회수.

## 구현

- `EnhancedRenderGraph::CaptureDiagnosticSnapshot`은 성공한 Compile의 선언/실행 index, 컬링, usage, 수명, transition/UAV barrier, 기존 역방향 reachability edge를 값으로 복사한다. GPU 자원을 소유하지 않는다. 새 선언·Reset·실패한 Compile 뒤의 오래된 snapshot은 허용하지 않는다.
- 계약은 `legacy-declaration-order`·`inferred-from-state`·`versionsSupported=false`다. 현행 그래프가 버전 DAG나 Modify 의미를 지원하는 것처럼 표현하지 않는다.
- 기존 `render.live.capture <새 절대 경로> editor controlled` 제품 경로에 그래프·CPU compile/record·해상도·실프레임 종류·IBL 샘플 계약·VRAM snapshot을 추가했다. DX12는 해당 capture submission의 profiler token을 회수하고, Vulkan은 capture 전용 timestamp query pool을 사용한다.
- controlled capture는 render clock과 SSAO sample index를 0으로 하고 SSGI/fog history를 재시작한다. simulation clock·애니메이션을 재생하는 기능은 아니다.
- `verify-render-base0.ps1`은 입력 프로젝트를 별도로 복제하고 각 backend마다 독립 프로세스 2개, 프로세스당 캡처 2개를 만든다. 자산·tuning·환경·실행 파일·runtime DLL·소스 신원을 검사한다. GPU 검증 레이어, query 누락, nonfinite, shutdown 오류를 합격으로 처리하지 않는다.
- `base0_artifacts.py`는 7개 float32 첨부 이미지의 선형 수치 오차·RMSE·차영상과 final PNG를 생성한다. 기존 오차 상한 `0.002 + 0.005 * max(abs(a), abs(b))`를 유지한다. 입출력 identity가 다르면 이미지 비교 이전에 실패한다.
- 네이티브 그래프 fixture와 실제 live pixels는 별도 gate다. 변이는 입력·해상도·프레임 종류·순서·edge·수명·GPU 누락·품질 설정·barrier 누락·실제 픽셀 변경의 10종을 검사한다.

## 재현 계약의 범위

현재 계약명은 `asset-reconstructed-sealed-input-v1`이다. 고정된 자산과 설정으로 제품의 immutable scene 입력을 각 프로세스에서 재구성하고 카메라·광원·draw world·재질 typed bytes·geometry generation·LX pose를 비교한다. process-local material slot, 포인터, frame ID를 같은 값으로 강제하지 않는다.

선택한 fixture는 `LX_CookFixture.creator`의 정지 장면이다. 범용 직렬화 frame packet을 주입하는 replay, 움직이는 카메라/애니메이션, 모든 재질 route·SSS·투과 품질 수용은 이 실행으로 증명하지 않는다. 기존 MAT-9 미달을 BASE-0 통과로 닫지 않는다.

측정 scope는 `capture-submission-including-readbacks`다. 캡처 제출의 CPU compile/record 및 pass GPU 시간이며 일반 실행 성능 합격값이 아니다. VRAM은 device budget snapshot이고 transient peak가 아니다. cold shader/PSO readiness는 측정 밖에서 기다리되 시간 초과는 실패로 남긴다.

## 검증 현황

- VS18/v145 Debug·Release Editor 빌드 성공. 기존 linker warning은 별도이며 빌드 오류는 없었다.
- Debug 네이티브 `dx12.rendergraph`에 `BASE0_GRAPH_FIXTURES_OK` 소비 marker 확인. 기존 7개 그래프 검사도 통과했다. stale incremental test library 문제는 RenderTests 재빌드 후 해결했다.
- `Build/Obj/RenderBase0/Debug-v4`: DX12 동일/독립 프로세스 이미지 7개 모두 bit-exact, 10개 변이 거부. 전체 결과는 Vulkan 준비 시간 초과로 **실패**. 실행 중 sourceChanges=0.
- `Build/base0-vulkan-stacks.log`·`Build/base0-vulkan-stacks-v5.log`: 실패 실행에서 `SceneLookupCache::Initialize → VulkanPipelineCache::GetOrCreateCompute → NVIDIA driver compiler` 호출을 확인했다. GPU capture profiler가 활성화되기 전 단계였다. 이것만으로 영구 deadlock이나 일반 실행 GPU 성능을 판정하지 않는다.
- `Build/Obj/RenderBase0/Debug-v5`: DX12 반복/독립 프로세스 통과, Vulkan 0번 실행에서 실제 대기 600,029ms 뒤 timeout (`completedFrame=3873`, `afterFrame=3873`, `publishedFrame=41979`). 따라서 전체 gate 실패, sourceChanges=0. 같은 장시간 준비를 반복하는 Vulkan 1번 실행은 작업자가 중단했으며 합격 증거로 쓰지 않는다.
- `Base0VulkanTimingProbe` Debug·Release: 별도의 실제 Vulkan device와 4개 기록 worker로 clear 4개 + readback 1개를 3회 제출했다. 매번 5개 timestamp 회수, 예상 readback 색, validation=0, clean shutdown 확인. 전체 LX 장면의 Vulkan 준비·픽셀 gate를 대체하지 않는다. 이 검사도 하네스 선행 gate로 연결했다.
- capture profiler는 stack 수명이며 graph slot은 재사용된다. scope 종료/오류에서 graph의 profiler binding을 지우는 수명 보호를 추가하고 Debug·Release Editor를 다시 빌드했다.
- `Build/Obj/RenderBase0/Release-final-dx12-v2`: 수정본 Release Editor의 네이티브 graph marker·Vulkan timing fixture·DX12 2개 프로세스 × 2개 캡처·각 10종 변이·독립 프로세스 비교 통과. 7개 첨부 이미지 bit-exact, GPU query 누락 0, sourceChanges=0. `artifactGatesPassed=true`, DX12 단독이므로 `complete=false`.
- `Build/Obj/RenderBase0/Debug-final-dx12`: 수정본 Debug Editor에서도 같은 gate 전부 통과, 7개 첨부 이미지 bit-exact, GPU query 누락 0, sourceChanges=0. `artifactGatesPassed=true`, DX12 단독이므로 `complete=false`.
- `Build/Obj/RenderBase0/Debug-Release-parity`: 두 구성의 입력 자산·tuning·환경 신원이 일치하며, 실제 이미지 7개도 bit-exact. 동일 input/graph hash 확인. 단일 정지 fixture의 결과이며 모든 장면의 품질 수용을 뜻하지 않는다.
- 초기 슬라이스 판정: **하네스·DX12 정지 기준선 확보, BASE-0 전체는 진행 중**. 이 시점의 Vulkan 전체 장면 준비 미통과는 아래 후속에서 회수한다. IBL 샘플 수나 이미지 상한을 낮춰 gate를 우회하지 않는다. RG1~RG9·MAT-9 완료로 올리지 않는다.

환경은 RTX 4070 Ti / WMI driver `32.0.15.9597`, fixture 출력 332×202, IBL 1024/4096이다. 각 구성 capture 4개에서 graph 47 pass / GPU 49 slice, VRAM used 830~831MB / budget 11228MB였다.

| 캡처 제출 관측 범위 | Debug | Release |
|---|---:|---:|
| CPU compile ms | 0.3245~0.5017 | 0.0525~0.0792 |
| CPU record ms | 1.8973~10.4057 | 1.8047~9.7463 |
| GPU busy ms | 1.7536~2.5923 | 1.7413~1.9571 |
| GPU queue span ms | 1.8434~2.6881 | 1.8307~2.0552 |

적은 수의 진단 제출 관측값이며 성능 합격이나 Debug 대비 개선을 주장하지 않는다.

## Vulkan cold PSO 조사 후속 (2026-10-01)

`Base0VulkanPsoProbe`는 SceneLookupBake의 strict-math SPIR-V와 실제 제품 root layout을 사용해 shader compile과 `vkCreateComputePipelines` 시간을 분리한다. 캡처나 GPU 실행 시간의 대체 지표가 아니며, 시간 초과 실행은 소유한 probe 프로세스만 종료했다.

| 임시 셰이더/드라이버 변형 | 관측 결과 |
|---|---|
| 큰 샘플 루프에 `[loop]` | 60초 시간 초과 |
| 원본 + driver disable optimization | 60초 시간 초과 |
| Thin Film의 두 `[unroll]`도 `[loop]`로 변경 | 60초 시간 초과 |
| 내장 테이블 전체를 상수로 단순화 | PSO 2.466초, validation=0; 품질 변경 진단이며 제품 적용 금지 |
| Thin Film sensitivity 조회만 상수로 단순화 | PSO 5.026초, validation=0; 품질 변경 진단이며 제품 적용 금지 |
| 원본 값을 유지한 16-entry chunk / 512-case switch | 각각 60초 시간 초과; 제품 미적용 |
| 원본 sensitivity 조회 함수에 `[noinline]` | PSO 17.480초, validation=0; 전체 장면 두 실행은 device lost로 실패하여 제품 수정에서 제외 |
| 원본 sensitivity 값을 b1 상수 버퍼에서 조회 | PSO 5.426초, validation=0; 실제 장면 gate 별도 검증 |

원본 SPIR-V는 함수 1개/호출 0개이며, noinline 변형은 함수 2개/호출 30개/`DontInline` 함수 1개다. 큰 sensitivity 테이블 조회 확장이 준비 지연에 관여한다는 근거를 확보했다. [Slang의 noinline 계약](https://docs.shader-slang.org/en/latest/external/core-module-reference/attributes/noinline.html)은 downstream 함수 확장을 억제하지만 실행 성공을 보장하지 않는다. `Debug-noinline-v1`은 첫 준비를 63.892초에 넘겼으나, 두 독립 Vulkan 실행 모두 device lost 뒤 live renderer가 비활성화되어 캡처에 실패했다. command/descriptor pool 재사용·buffer destruction 검증 오류도 기록되었으며 최초 device lost의 단독 원인은 확정하지 않았다. 이 변형을 제품에서 제외했다.

적용한 수정은 **Vulkan SceneLookupBake에 한해** 동일 sensitivity 값을 b1의 float4 두 배열로 읽는다. DX12와 다른 셰이더는 기존 상수 조회를 유지한다. `generate-film-sensitivity-upload.py`가 canonical Slang의 real/imag 512행씩을 원래 리터럴 그대로 C++ float4 배치로 생성한다. 하네스 선행 검사 `--check`가 stale mirror를 거부한다. frame의 기존 upload arena가 16KiB와 dispatch constants를 제출 완료까지 함께 소유한다. 별도의 자원 pool이나 새 그래프 texture는 없다. 매 frame 업로드이며 GPU Scene 상주 설계 완료로 해석하지 않는다. 테이블 값, 보간, 1024/4096 샘플 수와 strict math는 유지한다.

진단 원본은 `Build/base0-pso-*.stdout.log`, `.stderr.log`, `.result.txt`와 `Build/Obj/RenderBase0/PsoInvestigation`에 보존한다. 폐기한 noinline 변형의 Debug DX12 수정 전/후 동일 입력 이미지 7개는 bit-exact였다 (`NoInline-Dx12-old-new-parity`). 최종 GPU-table 수정도 DX12 변경 전/후 7개 이미지 bit-exact다 (`GpuTable-Dx12-old-new-parity`). PSO probe는 실제 GPU dispatch나 전체 실행 성능의 수용 증거가 아니다.

### 실제 장면 후속 및 Release abort 회수

- `Debug-gpu-table-v1`: DX12/Vulkan 각각 독립 프로세스 2개 × 캡처 2개, 같은 backend의 7개 이미지 모두 bit-exact, 각 실행의 10종 변이 거부, validation=0, sourceChanges=0. Vulkan 준비와 device-lost 실패를 넘겼다. 당시 전체 결과는 물리 graph identity의 교차 차이 때문에 실패로 보존한다.
- `Base0FilmUploadProbe` Debug/Release: 실제 GPU에서 canonical shader table과 b1 업로드 table의 real/imag 512행씩을 모두 readback하여 bit-exact 확인, validation=0, 누락 encoder 호출 0, 정상 종료. `Build/base0-film-upload-{Debug,Release}-final.stdout.log` 참조. 표 데이터 보존 검사이며 모든 Thin Film 재질 이미지의 수용은 아니다. 원본 generator가 upload mirror 생성/검사도 함께 실행한다.
- `Release-gpu-table-v1`: DX12 정지/독립 프로세스 gate는 통과. Vulkan은 캡처가 생성됐지만 제품 Release 경로의 validation disabled를 하네스가 거부했다. 이를 합격으로 쓰지 않는다.
- `Release-gpu-table-v2`: scene 장치에만 Release validation override를 적용한 연결 누락으로 ImGui 장치와 설정이 달라졌다. `VulkanApi::LoadDevice`의 process-wide 진입점이 validation 경로로 바뀐 뒤 비검증 ImGui 장치 호출에서 `The VkDevice dispatch handle was not found` 및 `abort()`가 발생했다. 사용자에게 보인 보고서 `CreatorEditor_20261001_205904_66808`은 `ImGuiVulkanShell::RenderAndPresent`, `CreatorEditor_20261001_205933_70484`는 `ImGuiVulkanShell::NewFrame → GetCompletedFenceValue` 호출을 기록한다. 두 실행과 dump/text를 보존하며 합격 증거에서 제외한다.
- override를 `VulkanDeviceResources::Initialize`로 옮겨 `CREATOR_VULKAN_VALIDATION=on`이 **모든 장면/ImGui 장치**에 적용되게 했다. 일반 Release의 기존 기본값은 유지한다. scene-only override는 제거했다. 서로 다른 validation 정책의 임의 장치를 일반적으로 안전하게 혼합하는 per-device dispatch redesign 완료를 뜻하지 않는다.
- `Release-gpu-table-v3-vulkan`: 수정된 Release Editor 독립 2회 × 캡처 2개, 반복/프로세스 간 7개 이미지 bit-exact, 각 10종 변이 거부, validation enabled·오류 0, sourceChanges=0, 정상 종료, 새 crash dump 없음. `artifactGatesPassed=true`; Vulkan 단독 실행이므로 `complete=false`다. 전체 BASE-0 완료로 올리지 않는다.
- `Debug-gpu-table-final-vulkan`: 공통 초기화 수정 후 다시 빌드한 Debug Editor에서도 같은 2회 × 2개 캡처 gate 전부 통과, 7개 이미지 bit-exact, validation enabled·오류 0, sourceChanges=0. 최종 하네스 기록에서 두 프로세스 모두 `exitCode=0`, `forcedTermination=false`다. 양 구성의 실제 후속 판정은 `gpu-table-final-summary.json`을 따른다.
- 하네스는 실행별 exit code/강제 종료 여부를 보존하고, 첫 live 실행 실패에서 중단하여 같은 crash-report 창을 반복 생성하지 않는다.

### 교차 backend graph와 품질 판정

원본 graph는 backend별로 완전히 감사한다. 교차 비교에서만 SSGI physical ring 0/1을 `Previous/Current` 역할로 바꾸고, DX12의 `Live.Shared`를 단독 `live_present`의 imported CopyDest leaf일 때만 제외한다. 추가 소비자·잘못된 read/write 연결은 거부한다. 제품 Pass 변경은 여전히 hash 차이로 검출한다 (`LogicalGraphProjectionTests.json`). 동일 backend 비교는 기존 전체 topology 신원을 유지한다. 이는 실행 DAG나 resource version 구현이 아니다.

Debug의 `Debug-gpu-table-cross-recheck`와 Release의 `Release-gpu-table-cross-final`은 실제 같은 입력에서 같은 결과를 기록했다. logical graph hash는 `89097022d75d14a1da8f47e181bedf114903496d687148188b41cdae1f8b3958`이다.

| 실제 교차 이미지 | 최대 절대 오차 | 기존 상한 초과 픽셀 |
|---|---:|---:|
| baseColor / depth / emissive / metalRough / normal | 0 | 0 |
| preToneHdr | 0.8671875 | 32634 |
| display | 0.031372547 | 20553 |

절대+상대 상한 `.002 + .005 * max(abs(a), abs(b))`을 유지했다. GBuffer 5종 일치 이후 HDR/표시 차이를 확인했으나, 조명·IBL·히스토리·후처리 중 최초 차이의 단독 원인은 이 첨부들만으로 확정하지 않는다. **BASE-0은 이 교차 품질 gate와 범용 밀봉 packet replay 때문에 계속 진행 중**이다. MAT-9 SSS/투과 및 이동 성능 완료와도 별도다. 초기 332×202 정지 fixture의 진단 결과로 성능 수용을 주장하지 않는다.

## 실행

PowerShell 7과 Python 표준 라이브러리를 사용한다. BASE-0은 Editor를 같은 Configuration으로 빌드하고 `-Backend dx12`로 실행한다. Vulkan timing probe는 4.9의 명시적 진단 실행에서만 필요하다. 기존 산출물을 덮지 않도록 새 출력 디렉터리가 필요하다.

```powershell
& Tools/regression/verify-render-base0.ps1 -Configuration Debug `
  -FixtureProject '<입력 프로젝트 절대 경로>' `
  -OutputDirectory '<새 산출물 절대 경로>'
```

`-Configuration Release`로 같은 입력을 별도 실행한다. DX12의 `artifactGatesPassed`와 범용 replay 완료 여부를 구분한다. Vulkan 비교 여부는 BASE-0 완료 조건이 아니다. 실제 입력·실행 환경·known gaps·세부 판정은 각 출력의 `result.json`, `source-hashes.json`, `input-files.json`, `hardware.json`, capture manifest 및 comparison artifact에 남는다.


## 단계별 HDR 진단과 capture history 수명 수정 (2026-10-01)

캡처 요청에 한해 LivePipelineDesc의 활성 노드 선언 직후 HDR readback을 추가했다. 이 fixture는 Deferred, LX.Scene.Color, SkyBox, SSGI, Forward+, Sprite, SSS, SSR, LX.Scene.Volume의 9개 중간 결과를 저장한다. 기존 7개 attachment는 유지하고 `diagnosticStages`를 별도로 기록한다. 비교기는 단계의 크기·유한값·파일 경로·바이트 수·양쪽 집합을 검사하고 같은 오차 상한을 적용한다. 동일 단계 통과와 실제 단계 픽셀 변조 거부를 확인했다. 캡처 제출에는 추가 readback 비용이 들어가므로 이전 제출 시간과 직접 성능 비교하지 않는다.

`Debug-hdr-stages-v2`는 양 backend 각각 독립 2회 × 캡처 2개가 정상 종료·validation 오류 0·반복 일치를 통과했으나 교차 품질은 실패했다. 추가 진단 전후 DX12의 기존 7개 이미지는 bit-exact다 (`instrumentation-parity.json`). Deferred/LX.Scene.Color/SkyBox의 교차 최대 오차는 0.001953125로 기존 상한 안이고, SSGI 직후부터 HDR 최대 오차 0.8671875 / 상한 초과 32634픽셀이 발생했다. 최초 `Debug-hdr-stages-v1`은 runtime DLL 배치 완료 전에 실행한 fixture가 loader error=32로 실패했으며 수용 증거에서 제외한다.

Vulkan의 CaptureHistoryGuard가 resource_prepare 블록 안에 있어 PrepareFrame이 갱신한 SSGI sample index와 history ring을 블록 종료 때 다시 초기화하고 있었다. DX12는 캡처 프레임 전체에서 guard를 유지한다. Vulkan도 준비 전에 초기화하고 기록·제출 완료까지 guard가 살아 있게 수정했다. 일반 렌더의 비활성 guard와 IBL 1024/4096, 이미지 상한은 유지했다.

최종 `Debug-hdr-history-scope-v1`과 `Release-hdr-history-scope-v1`은 구성별 양 backend 독립 2회 × 캡처 2개를 검증했다. 모든 실행 exitCode=0, forcedTermination=false, validation 오류 0, 같은 backend의 기존 7개와 중간 9개 이미지 반복/독립 프로세스 일치, 각 10종 변이 거부, sourceChanges=0이다. 양 구성의 교차 결과도 같다.

| 교차 결과 | 수정 전 | history scope 수정 후 |
|---|---:|---:|
| HDR 최대 절대 오차 | 0.8671875 | 0.435546875 |
| HDR 상한 초과 픽셀 | 32634 | 466 |
| display 최대 절대 오차 | 0.031372547 | 0.011764705 |
| display 상한 초과 픽셀 | 20553 | 186 |

GBuffer 5종은 계속 bit-exact이며 남은 상한 초과는 SSGI 이후에서 시작한다. SSGI 내부 추적·누적·필터 중 잔여 오차의 단독 원인은 아직 미확정이다. 범용 sealed frame packet replay도 미구현이다. 하네스는 staticFixtureGatesComplete와 genericPacketReplayVerified를 구분하여 정지 fixture만으로 BASE-0 complete를 올리지 않는다. 현재 교차 gate는 여전히 실패로 보존하며 기준 변경이나 MAT-9 완료를 적용하지 않았다.
