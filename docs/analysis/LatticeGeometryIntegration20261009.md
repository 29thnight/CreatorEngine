# LX 메시렛 컬링·기하 LOD·현재 깊이 HZB 연결

2026-10-09. 기준 master는 `cd3fa2e2f8cf40d4c810731430f8be4503a09f1e`이며,
작업 종료 시 원격 master도 같은 해시였다. 아래 구현은 현재 작업 트리의 후속 변경이다.
기존 RG8/GCCE 및 workspace 변경은 작업 범위에 포함하지 않았다.

## 구현

- **메시렛별 프러스텀 컬링:** `LXSceneMS`와 `LXSceneShadowMS`가 각 메시렛의
  변환·스키닝 완료 정점을 카메라/해당 cascade의 clip 공간에서 검사한다.
  모든 정점이 같은 clip 평면 밖에 있을 때만 출력 정점·삼각형 수를 0으로 만든다.
  평면 교차·비유한 값은 유지하며 경계 오차 여유를 둔다. bind-pose 구나 cone을
  변형된 메시의 거절 근거로 사용하지 않는다. 모든 mesh group은 여전히 dispatch된다.
- **기하 LOD 선택과 제출:** `SelectSceneGeometryLod`가 저장된 `coarseLods`의
  simplification error를 화면 오차로 투영한다. 기본 한도는 1 pixel이며,
  직교·원근·비균일 스케일/전단 및 근평면 교차를 다룬다. 선택은 CPU에서 수행한다.
  선택한 level의 실제 인덱스를 seal/partition/변환/토폴로지/간접 bin까지 전달한다.
  정적 partition 캐시는 mesh handle와 LOD를 함께 키로 사용한다.
  저장된 LOD가 없거나 bone palette가 활성화되었거나 오차/투영이 불확실하면 LOD0이다.
  simplifier error는 품질 지표이며 인증된 Hausdorff 거리로 해석하지 않는다.
- **독립 그림자 제출:** 카메라가 낮은 LOD를 선택하더라도 그림자는 별도 LOD0
  plan/batch/bin을 유지한다. 광원별 screen-error 정책이 없는 상태에서 카메라 거리로
  캐스터 품질을 낮추지 않는다. 추가 CPU/GPU payload도 view의 합산 예산에 포함한다.
  중복 geometry 선언을 피하고 두 제출 집합 모두 완료점까지 보유한다.
- **라이브 HZB:** RG2에서 앞선 occluder depth가 없으면 `LX.Scene.OccluderDepth`가
  별도 D32 texture를 clear 1로 초기화하고 불투명·마스크 재질의 깊이를 기록한다.
  alpha 평가와 pixel-center 복원은 GBuffer와 같은 계약을 사용한다.
  그 깊이로 pyramid와 `Geometry.Visibility.CullOcclusion`을 만들고 본 GBuffer를 제출한다.
  본 depth를 선행 패스로 덮어쓰지 않아 기존 Less 및 첫 primitive의 동률 소유권을 보존한다.
  투명·투과 재질은 occluder에서 제외한다. HZB 거절 단위는 draw이며 메시렛 단위 HZB가 아니다.
  history/reprojection을 도입하지 않았으며 compatibility graph는 프러스텀 경로를 유지한다.
- **토폴로지 반복 생성 방지:** 최대 descriptor/remap/triangle 크기의 상한으로
  128 MiB·256-entry cache 수용 가능성을 먼저 검사한다. 수용 불가능하면 생성 전에
  transient indexed 경로로 내려간다. `CREATOR_LX_MESHLETS=0`이면 토폴로지를 만들지 않는다.
  상한 예약은 실제 크기보다 보수적이므로 여유가 작은 cache에서 indexed로 갈 수 있다.
- **셰이더 계약:** 깊이 pixel entry를 공통 Scene compile/cook/load/ShaderMeta pass에
  포함하고 Scene host identity를 18로 올렸다. 이전 Scene cooked stage set은 재-cook이 필요하다.
  `CREATOR_LX_HZB=0`은 HZB/선행 깊이 패스를 끄는 rollback 경로다.

주요 소스: `MaterialGraphSceneLod.h`, `MaterialGraphSceneInput.cpp`,
`MaterialGraphSceneHost.cpp`, `MaterialGraphMeshSurface.cpp`, `MaterialGraphSceneCompiler.cpp`,
`Includes/MaterialGraphSceneHost.slang`.

## 검증

| 증거 | 결과 | 범위 |
|---|---|---|
| Editor x64 Debug/Release build | 모두 성공 | RenderEngine 및 실제 Editor 소비자 |
| GPU 회귀 Debug | 4조합, 96프레임, 228,980검사, 42,984 GPU component 비교, validation 0 | indexed/mesh × HZB OFF/ON, immediate/1-worker/4-worker |
| GPU 회귀 Release | 4조합, 96프레임, 228,980검사, 42,984 GPU component 비교, validation 0 | 같은 조건, 독립 Core/Layered 조명·소유권·깊이 참조 |
| LOD 수치 회귀 | 각 실행의 7조건 통과 | 직교 화면 크기, 원근 거리, 큰 화면 오차, 비균일 scale, 근평면, NaN error |
| 실제 Release DX12 TestShadow | 2 sample pair × 16 attachment = 32개 모두 비트 동일 | 현재 소스의 indexed/mesh 출력 비교, 두 모드 GPU validation problems/drops 0 |
| 실제 mesh 호출 | surface/shadow 모두 `groups=1 indirect=1` 첫 호출 로그 확인 | 호출 증거이며 총 메시렛 수나 visible count가 아님 |
| 정적 cache 검사 | 생성 전 예산 검사·OFF 시 생성 건너뛰기 확인 | cache pressure 실행/성능 gate의 대체 증거가 아님 |

재현: `Tools/regression/verify-lx-meshlet-geometry.ps1`.
각 소스 hash와 실행 binary hash의 불변성을 검사하며 4조합 모두 실행한다.
TestShadow는 기존 `verify-lattice-meshlets.ps1 -Samples 2 -Configuration Release -ValidateGpu`로 실행했다.

실행 증거는 `Build/lx-geometry-debug-validation`, `Build/lx-geometry-release-validation`,
`Build/lx-geometry-live-release`에 있다. Editor build 로그는
`Build/lx-meshlets-editor-build-Debug.log`와 `Build/lx-meshlets-editor-build-Release.log`다.
Build 경로는 실행 산출물이며 저장소에 커밋하지 않았다.

## 전체 GPU geometry 수용과의 경계

이번 변경으로 LX에 메시렛별 프러스텀 검사, CPU 기하 LOD 선택·제출, 현재 깊이 draw HZB가 연결됐다.
GPU LOD 선택, visible-meshlet 목록 압축/간접 group 수 감소, 메시렛별 HZB,
광원별 LOD 품질 정책은 구현하지 않았다. 실제 저장 LOD 전환 장면의 GPU 품질 수용,
새 cache pressure/overflow 시나리오, Vulkan, 다중 view/큰 workload, Release 성능·VRAM은 별도 gate다.
따라서 GPU-1/전체 GPU-driven geometry의 완료·기성으로 계상하지 않는다.
