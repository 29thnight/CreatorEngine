# 에디터 모델 대체 재질 분리 구현 및 실제 드래그 비교 — 2026-10-21

## 결과

Debug x64 재빌드와 실제 콘텐츠 브라우저 → 씬 드래그 앤 드롭 비교를 수행했다. 모델을 회색 대체 재질로 먼저 배치하고 선택·편집할 수 있게 한 다음, 원본 재질을 백그라운드에서 준비해 같은 모델에 교체한다.

**표시와 원본 재질 완성의 분리는 동작한다. 전체 원본 완성 시간까지 단축된 것은 아니다.** 빈 캐시에서 대체 재질의 런타임 컴파일 비용이 남아 있으며, 원본 작업을 첫 대체 프레임 제출 후 시작하는 정책 때문에 최종 원본 완성은 기존 방식보다 늦어졌다. UE의 이미 준비된 기본 재질과 같은 즉시 표시 수준에는 아직 도달하지 않았다.

## 조건과 계측의 의미

- `Bin/x64-Debug/Editor/CreatorEditor.exe`를 직접 실행. 디버거 미부착, 실제 보이는 창, DX12 DebugLayer on / GPUValidation off. 성능 표의 모든 모델 배치는 네이티브 마우스 드래그다. CLI 모델 로드는 성능 표에 사용하지 않았다.
- 각 조건은 새 프로세스로 시작했다. 모델마다, 기존/새 방식마다 독립된 빈 캐시 디렉터리를 만들었으며 cold 실행 시 항목 수는 모두 0이었다. warm은 해당 cold 실행에서 만든 디렉터리를 그대로 재사용했다.
- RHI 캐시뿐 아니라 생성된 재질 프로그램 캐시도 `CE_MATERIAL_CACHE_ROOT`로 분리했다. 원본 GLB, 모델 임포트 산출물, OS 파일 캐시, GPU 드라이버 자체 캐시는 초기화하지 않았다. 따라서 여기서 빈 캐시는 **엔진 셰이더/재질 프로그램 캐시**를 뜻한다.
- 비교는 동일한 최종 바이너리에서 `CE_EDITOR_MODEL_PREVIEW=0`인 기존 전체 재질 대기 경로와, 기본값인 새 대체 경로를 비교했다. 과거 커밋의 별도 바이너리와 비교한 것은 아니다.
- 최종 runtime DLL SHA256: `0B9755256DDB325AD11A964ABCBE06608CD2B4FD490E7E26124DE0F568BCA360`. 최종 비교 8회 및 추가 확인 2회 모두 동일하다.
- 화면 관찰은 드래그 입력 직전부터 측정했다. 렌더 로그는 배치 요청 수락부터 측정한다. 두 시작점은 드래그 이벤트 처리만큼 차이 난다.
- `model.display`는 해당 재질이 선택된 모델 draw를 포함한 프레임의 **GPU 제출 수락 시각**이다. Present 완료 또는 첫 픽셀 시각이 아니다. `model.materials`는 CPU 측 원본 교체 시각이며 화면 완성으로 취급하지 않는다.
- 스크린샷에서 마지막 미표시와 첫 표시 사이를 `(a, b]`로 표시했다. 선택/이동 시각은 실제 입력을 보내 응답을 확인한 상한이며, 그때까지 에디터가 멈춰 있었다는 뜻이 아니다. 관찰 사이의 작업자 공백을 로딩 시간으로 더하지 않았다.
- 각 조건 1회, CreatorRobot cold 추가 1회와 baseline warm 추가 1회다. 평균/백분위 또는 통계적 개선율을 주장할 수 없다.

## 렌더 제출 시각 비교

단위: 드롭 요청 후 초. 기존 방식은 첫 모델 제출 때 이미 원본 재질이다.

| 모델 / 조건 | 기존 첫 제출 = 원본 | 새 대체 첫 제출 | 새 원본 제출 |
|---|---:|---:|---:|
| CreatorRobot / 빈 캐시 | 39.107 | 33.359 | 58.542 |
| CreatorRobot / 재실행 | 4.426 | 2.890 | 6.258 |
| Aria / 빈 캐시 | 32.331 | 24.041 | 47.325 |
| Aria / 재실행 | 3.509 | 2.375 | 5.576 |

CreatorRobot은 대체 상태 편집을 추가 확인하려고 별도 빈 캐시에서 다시 드롭했다. 이때 대체 제출 **24.504초**, 원본 제출 **49.571초**였다. 첫 cold의 33.359/58.542초와 모두 보존한다. 대체 컴파일 및 공통 셰이더 비용은 유사했으나 첫 제출까지 약 9초 차이가 있어 드라이버 PSO 또는 스케줄링만으로 원인을 확정하지 않는다.

CreatorRobot baseline warm의 첫 화면 관찰을 늦게 해서 해당 시각으로 로딩 시간을 주장할 수 없었다. 같은 캐시로 새 프로세스에서 다시 확인했으며 원본 제출은 **4.426초**, 실제 첫 화면은 **(4.015, 5.098]초**였다. 원래 실행의 제출값 4.426초도 유지했다.

## 실제 화면 및 조작 확인

단위: 드래그 입력 직전부터 초. 아래 선택/이동 시각은 최초 준비 가능 시각이 아니라 **실제 조작 성공을 확인한 시각**이다. 이를 서로 빼서 응답성 개선율로 해석하면 안 된다.

| 모델 / 방식 / 조건 | 첫 모델 화면 범위 | 선택 응답 확인 | 이동 응답 확인 | 원본 재질 화면 범위 |
|---|---:|---:|---:|---:|
| Robot / 기존 / cold | (33.586, 48.147] | 48.147 | 63.899 | (33.586, 48.147] |
| Robot / 기존 / warm 추가 확인 | (4.015, 5.098] | 22.015 | 40.066 | (4.015, 5.098] |
| Robot / 새 / cold 추가 확인 | (12.039, 25.344] | **40.191, 회색 상태** | 56.674, 원본 상태 | (40.191, 56.674] |
| Robot / 새 / warm | (0.878, 7.134] | 14.139 | 25.370 | (0.878, 7.134] |
| Aria / 기존 / cold | (29.514, 38.284] | 38.284 | 49.652 | (29.514, 38.284] |
| Aria / 기존 / warm | (0.282, 8.501] | 19.265 | 30.548 | (0.282, 8.501] |
| Aria / 새 / cold | (20.057, 31.729] | **31.729, 회색 상태** | **45.229, 회색 상태** | (45.229, 57.440] |
| Aria / 새 / warm | (2.450, 15.117] | 15.117 | 28.653 | (2.450, 15.117] |

Aria는 원본 준비 중 Z 위치를 0.219에서 0.870으로 드래그 이동했고, 원본 재질 전환 후에도 Z=0.870을 유지했다. CreatorRobot도 회색 모델 선택과 Transform Inspector/gizmo 응답을 확인했다. Robot 이동 확인 때에는 이미 원본 재질이었으므로, 해당 실행으로 **원본 전 이동 성공**까지 입증했다고 주장하지 않는다. 두 모델 모두 최종 원본 상태에서 실제 이동이 됐다.

첫 Robot 새 cold의 화면 관찰은 22.062초 미표시 이후 105.863초 원본+선택으로 간격이 길었다. 이 관찰로 첫 표시를 105초라고 판단하지 않고 추가 cold 확인을 수행했다. 그 로그와 이벤트도 삭제하지 않았다. warm에서는 대체가 짧게 지나가 스크린샷으로 회색 상태를 잡지 못한 실행이 있다. 이 경우 대체 제출 로그와 원본 화면 확인을 구분했다.

## 컴파일·캐시·대기 증거

드래그 시작 시 stderr byte offset 이후만 집계했다. 단계별 `sum`은 여러 스레드의 누적값이므로 실제 경과 시간과 다르다. `span`은 해당 요청 묶음의 처음부터 마지막 완료까지다.

| 실행 | 원본 실제 컴파일 / disk hit | 원본 code 생성 누적 | 원본 작업 span | 원본 wait 누적 |
|---|---:|---:|---:|---:|
| Robot 기존 cold | 64 / 0 | 78.819초 | 20.869초 | 0.794초 |
| Robot 새 cold | 64 / 0 | 86.032초 | 22.851초 | 0.396초 |
| Robot 기존 warm | 0 / 64 | 0 | 1.176초 | 0.779초 |
| Robot 새 warm | 0 / 64 | 0 | 1.138초 | 0.749초 |
| Aria 기존 cold | 48 / 0 | 56.054초 | 20.256초 | 0.388초 |
| Aria 새 cold | 48 / 0 | 58.746초 | 21.124초 | 0.127초 |
| Aria 기존 warm | 0 / 48 | 0 | 0.973초 | 0.413초 |
| Aria 새 warm | 0 / 48 | 0 | 0.997초 | 0.384초 |

- 새 대체 재질은 cold에서 16개 entry를 실제 컴파일하며 span이 Robot 12.968초 / Aria 12.833초다. warm에서는 16개 disk hit, 컴파일 0으로 0.583초 / 0.560초다.
- 공통 렌더 helper는 cold마다 46개 컴파일, 요청 누적 약 9.6~9.9초다. warm은 46개 disk hit, 컴파일 0이다.
- 모든 최종 UI 측정에서 shader 실패 0. 요청 큐 대기는 trace상 0이며 위 wait가 전체 수십 초 지연을 설명하지 않는다.
- 원본 CPU 교체 후 실제 원본 제출까지 새 방식은 약 1.9~2.0초가 남았다. native program/PSO 준비 및 프레임 반영을 포함하는 구간이지만, 이번 trace로 그 안을 정확히 나눠 귀속하지 않았다.

원본 컴파일 20~23초를 앞에서 전부 기다리던 연결은 제거했다. 다만 대체 셰이더 자체를 사용 시 생성하는 약 13초와 첫 공통 helper 준비 약 10초가 아직 첫 표시를 막는다. **1분 지연 전체를 제거했다고 주장할 수 없다.** 새 cold 원본 제출은 실제로 47~59초까지 걸렸으며, 첫 표시를 앞당기는 대신 최종 준비를 뒤로 미루는 정책적 비용이 확인됐다.

## 구현

1. `EditorModelPlacement.cpp`에서 에디터 일반 모델 배치만 대체 경로를 기본 적용한다. 기존 전체 재질 warmup을 첫 배치의 필수 조건에서 제외했다. typed prepared 모델과 gameMode 경로는 기존 동작을 유지한다.
2. 안정된 임시 그래프 ID의 중립 회색 emission 재질을 사용한다. 대체 프로그램만 Slang `-O1`, 원본은 기존 `-O3`를 유지한다. 16개 shader entry와 reflection/validation 계약을 유지하고 O1 캐시 키를 별도로 버전 지정했다.
3. 기존 모델 generation을 소유한 상태로 geometry·계층·애니메이션을 점진 배치한다. 첫 대체 프레임의 제출 확인 후 bounded worker 작업으로 사용 중인 원본 재질만 준비한다. 화면 밖 모델도 영구 대기하지 않도록 scene 배치 후 최대 30초에서 원본 작업을 시작한다.
4. 원본 CPU 준비가 끝나면 같은 EntityHandle의 MeshRenderer 재질만 교체한다. transform/계층/Animator를 다시 만들지 않는다. 사용자가 별도 재질로 바꿨거나 대체 인스턴스를 편집했다면 늦은 원본 결과가 덮어쓰지 않도록 pointer/instance 확인을 한다.
5. 원본 GPU 프로그램이 준비되는 동안 명시적으로 표시된 에디터 대체 재질만 유지한다. material slot identity를 유지하고 실제 선택 재질이 바뀔 때 temporal revision을 갱신한다. 일반 재질 오류에 전역 fallback을 추가하지 않았다.
6. 준비 중 저장할 때는 원본 graph ID와 원본 blend 정보를 쓴다. 임시 회색 그래프를 scene의 원본 재질처럼 저장하지 않는다. Inspector에서는 준비 상태를 보여주고 대체 파라미터 편집을 제한하며 재질 교체는 허용한다.
7. 취소·Undo·씬 전환은 기존 handle 검증으로 늦은 결과를 거절한다. 진행 중 worker를 reaper가 보존하며 종료 시 두 작업을 join한다. 오류가 나면 회색 모델을 남기고 오류를 보고하며 원본 완성으로 표시하지 않는다.

주요 코드:

- [배치와 원본 작업 분리](C:/Users/idene/source/repos/CreatorEngine/Editor/EngineEntry/EditorModelPlacement.cpp:138)
- [같은 모델에 원본 재질 적용](C:/Users/idene/source/repos/CreatorEngine/Engine/SceneRuntime/ModelSceneInstantiation.cpp:76)
- [GPU 준비 중 대체 유지](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/MaterialGraphSceneHost.cpp:1713)
- [제출 확인 및 phase 계측](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/MaterialGraphSceneHost.cpp:4619)
- [대체 상태의 원본 식별자 저장](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/DataSystem.cpp:3979)
- [대체 전용 컴파일 최적화](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/RHI/RHIShaderCompiler.cpp:1701)

## 검증과 보존

- 최종 Debug x64 빌드 성공: `Build/editor-fallback-drag-20261021/build7.log` 및 위 DLL hash. 실제 창 10회 실행 모두 exit 0.
- 최종 O1 셰이더 캐시 회귀 검사 통과: codec, restart, transitive/priority include, profile, strict math, permutation, DXIL/SPIR-V probe reflection, 손상 복구, 쓰기 실패, 동시 요청. 대체/원본 캐시 분리와 probe binding 유지 검사 포함. `shader-cache-tests/Run-66d05962ef1f432cb69f532db72256e7`.
- 기능 commandlet 통과: `lifecycle-commandlet2/summary.json`. Robot 대체 프레임 제출 후 원본 준비 중 scene 저장, 원본 graph ID 4개 유지, 임시 그래프 저장 0, Undo 후 Robot 누출 0, Aria 준비 중 씬 전환 후 누출 0, 취소 2, 종료 0. 이 기능 검사는 offscreen이며 성능 표와 구분한다. Aria의 씬 전환은 준비 중 취소 검사로, 대체 표시 이후 취소까지 검증한 것으로 확장하지 않는다.
- 최초 기능 스크립트는 일반 console에 commandlet 전용 `model.async`를 보내 실패했고, 첫 commandlet 시도는 모델 배치 전 너무 일찍 저장해 assertion 실패했다. 실행 모드 및 대기 조건을 수정한 최종 위 게이트만 통과로 기록한다.
- 변경한 소스에 `git diff --check` 통과. 프로젝트 전체의 다른 기존 변경을 정리하거나 되돌리지 않았다. Player/typed 모델의 실행 검증은 수행하지 않았다.
- 기존 사용자 RHI cache: 263,128개 → 263,128개, 변경/누락 0, 추가 0. 비교는 파일명·크기·mtime 기준이며 전체 내용 hash 비교는 아니다. 원본 `LOCALAPPDATA`를 덮어쓰거나 캐시를 삭제하지 않았다.
- 두 원본 GLB SHA256 유지: Robot `58D779AFDE1A7FBD13332999C8B11890FB7E10FF2036CA6AE3645D14BEDBA402`, Aria `8ED22A7E3CF7E4F7087A45E83EEBF327B283F0162622F0051CF8AE45833FB0E7`.
- 모든 에디터를 종료한 뒤 task 시작 시점의 `active.workspace` byte 백업으로 복원했다. 두 SHA256 `D748399BF2184E3A20357E8730A4E7D7F5C26F3BA491ABD1E52E5F4375F791DC` 일치.

## 남은 우선순위

첫 표시를 더 줄이려면 대체 그래프와 공통 렌더 프로그램을 빌드/배포 또는 에디터 시작 단계에서 준비해 드롭의 사용 시 컴파일에서 빼야 한다. 그다음 원본 작업의 GPU 제출 대기 구간과 약 9초 cold 편차를 별도 native program/PSO·Present 계측으로 나눠야 한다. 이번 구현은 UE식의 대체 재질/원본 분리를 적용한 것이며, UE의 기본 재질 배포·DDC·PSO 준비 전체를 구현한 것은 아니다.

원시 증거: [comparison.json](C:/Users/idene/source/repos/CreatorEngine/Build/editor-fallback-drag-20261021/comparison.json). 각 run 디렉터리에 `execution.json`, `ui-events.json`, `stdout.txt`, `stderr.txt`, `summary.json`을 보존했다. 대체 구현을 조정하던 이전 O0/O3 시제품 실행은 최종 비교에 섞지 않았다.
