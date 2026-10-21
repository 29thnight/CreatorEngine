# 모델 로드와 씬 배치 실측 보고서

2026년 10월 21일 · CreatorRobot 및 Aria_Dagger_ChainSwordWeapon

이 문서는 수정 전 기준선이다. 후속 수정과 재빌드 후 결과는 [무효화 복구 조건 수정 비교](ModelInvalidationRecoveryComparison-20261021.md)를 참고한다.

긴 로드 대기의 가장 큰 비용은 **씬 엔티티 생성보다 재질 프로그램 컴파일**이었다. 첫 실행에서 CreatorRobot의 재질 4개 컴파일에 69.84초, Aria의 재질 3개에 51.00초가 기록됐다. 이와 별개로 첫 렌더링에서 렌더 스레드가 2.21초를 사용한 프레임도 확인했다. 로드 대기와 화면 멈춤은 서로 다른 구간이므로 각각 개선해야 한다.

재시작 후 첫 모델에 남는 약 22~23초에는 **일시적인 재질 준비 무효화를 실제 재질 오류처럼 처리하는 복구 경로**가 관련돼 있다. 두 번의 재시작 모두 새 `Recovered_*.shadergraph`를 생성했고, 15~16초의 컴파일과 배치 준비 재시도 2회가 함께 관측됐다. 캐시를 추가하는 것과 함께 이 복구 조건을 바로잡는 작업이 우선이다.

## 측정 조건

- 기준 소스: `3531f1aa201f4d9e0db97c39442d2cd0bdd8b0cc`에 배치 계측 추가. Debug x64 Editor 및 의존 프로젝트를 다시 빌드했으며 빌드 종료 코드 0.
- CPU: Intel Core i7-8700K, 6코어 12스레드. 메모리 63.9GiB. GPU: RTX 2080 Ti, 드라이버 `32.0.16.1088`.
- 실제 Editor를 `--development-project Dynamic_CPP --smoke-offscreen --commandlet-script`로 실행. DX12 Debug Layer 켜짐, GPU Validation 꺼짐. 헤드리스 함수 호출만 실행한 결과가 아니다.
- 카메라와 조명을 만든 임시 씬에서 Content Browser 배치가 사용하는 `ModelPlacement::Execute` 경로를 `model.async`로 호출했다. 마우스 드롭 이벤트 자체의 지연은 측정하지 않았다.
- 실행 순서: 첫 실행 Robot→Aria, 재시작 Aria→Robot, 다시 재시작 Robot→Aria. 각 실행에서 두 모델을 다시 배치했다. 디스크 캐시 및 OS 파일 캐시는 지우지 않았다. 첫 실행 전에는 두 모델의 sidecar가 없었고 실행 중 생성됐다. 따라서 완전히 통제된 디스크 콜드 벤치마크로 해석하지 않는다.
- `totalMs`는 요청 생성부터 **씬 적용 완료**까지의 벽시계 시간이다. 첫 정상 화면 표시까지의 시간과 다르다. `prepareMs`는 준비 함수의 마지막 시도만 나타내며 자산 의존 작업 대기와 이전 재시도 시간을 포함하지 않는다.
- 렌더 출력 확인 해상도는 332×144. 이 결과를 일반 편집 창 해상도의 정상 프레임 성능이나 Release 성능으로 확대하지 않는다.

## 배치 시간

| 실행 조건 | CreatorRobot | Aria |
|---|---:|---:|
| 첫 실행 첫 시도 | 60초 대기 제한 초과, 이후 테스트 씬 전환으로 취소 | 53.320초 |
| 첫 실행 같은 프로세스 재배치 | 1.267초 | 0.256초 |
| 재시작 후 Aria부터 첫 배치 | 4.953초 | 21.879초, 준비 재시도 2회 |
| 같은 프로세스 재배치 | 0.985초 | 0.255초 |
| 다시 재시작 후 Robot부터 첫 배치 | 23.184초, 준비 재시도 2회 | 3.274초 |
| 같은 프로세스 재배치 | 1.002초 | 0.256초 |

첫 Robot 시도는 성공한 전체 로드 시간으로 제시할 수 없다. 취소 이후 작업까지 포함해 해당 준비 함수 자체는 70.379초 동안 실행됐고, 그 안의 재질 컴파일이 69.836초였다. 최초 자산 generation 준비는 별도 단계다. 두 시간을 합쳐 성공한 완료 시간처럼 제시하지 않는다.

Aria 첫 성공 시도는 준비 함수 51.327초, 전체 배치 53.320초였다. 재질 컴파일 50.997초는 준비 함수 시간의 약 99.4%다. 준비 중 3,185번의 배치 Tick이 진행됐고 최대 Tick 간격은 27.4ms였다. 이 53초 전체가 게임 스레드 정지였다는 뜻은 아니다.

씬 적용은 Robot 5~6프레임, Aria 14~15프레임으로 분할됐다. 완료된 요청의 최대 단일 `Advance` 시간은 1.714~3.629ms, 자산 게시 시간은 0.076~0.258ms였다. Aria의 본이 더 많아 적용 프레임은 늘었지만 수십 초 대기의 주원인은 아니었다. 2ms 예산은 개별 작업을 중간에 끊지 않으므로 엄격한 최대 지연 보장은 아니다.

재시작 실행 두 번은 모두 종료 코드 0이며 각각 4회 배치가 완료됐다. 각 캡처에서 Robot 4 draws와 깊이 픽셀 324개, Aria 3 draws와 깊이 픽셀 525개를 확인했고 출력은 finite였다. 이는 기본 표시 확인이며 재질 외관·애니메이션 품질 수용 검사는 아니다.

## 병목과 원인

### 1 재질 컴파일이 첫 로드 대기를 지배한다

| 첫 실행의 측정 구간 | 시간 | 해석 |
|---|---:|---|
| Robot `ModelPlacement.PrepareRecipe` | 70.379초 | 취소된 첫 요청의 준비 함수 |
| 그 안의 `Material.CompileSceneProduct` 4회 합계 | 69.836초 | 재질당 약 17~18초 |
| Aria `ModelPlacement.PrepareRecipe` | 51.327초 | 첫 성공 요청의 준비 함수 |
| 그 안의 `Material.CompileSceneProduct` 3회 합계 | 50.997초 | 재질당 약 15~18초 |

`ModelSceneInstantiation::PendingInstance::Prepare`의 generation 경로는 재질을 순회하면서 `ConfigureModelMaterialGraph`를 호출한다. 이 함수는 `LoadMaterialGraphGeneration`을 거쳐 `CompileAuthoringMaterial`의 `Material.CompileSceneProduct`에 도달한다. 모델 내부 재질 준비가 이 경로에서 순차적으로 진행돼 여러 재질의 비용이 누적된다.

디스크 캐시가 있는 재시작에서도 재질당 약 0.85초의 컴파일 범위가 관측됐다. 캐시 파일이 존재한다는 사실만으로 캐시 적중을 판정해서는 안 된다. `ReadMaterialGraphCache`는 header, 의존 파일 digest, include 경로와 cooked product를 확인하지만 실패 사유를 구분해 기록하지 않는다. 이번 계측으로 각 캐시 거절의 정확한 조건까지 확정하지는 못했다.

관련 코드: [준비 함수](../Engine/SceneRuntime/ModelSceneInstantiation.cpp), [재질 컴파일 및 캐시](../Engine/RenderEngine/DataSystem.cpp).

### 2 일시적 무효화가 새 복구 재질 컴파일을 유발한다

재시작 두 번의 로그에 다음 원인 문자열이 동일하게 남았다.

> Material generation preparation was superseded or invalidated before publication.

이후 서로 다른 `Recovered_48388e24-…`와 `Recovered_0223a5fb-…` 재질이 생성됐다. 두 실행의 가장 긴 `Material.CompileSceneProduct`는 각각 15.902초와 15.352초였다. 먼저 배치한 모델은 Aria일 때 21.879초, Robot일 때 23.184초였으며 모두 준비 재시도가 2회였다. 특정 모델 크기만으로 설명되는 현상이 아니다.

코드에서는 `ConfigureModelMaterialGraph`가 generation 취득에 실패하면 원인 종류를 나누지 않고 `ConfigureEditableDefaultMaterialGraph`로 들어간다. 복구 함수는 새 random GUID와 `.shadergraph`를 만들고 `ApplyAssetChange`를 호출한 뒤 복구 재질을 컴파일한다. 배치 측에는 별도로 무효화 준비를 다시 요청하는 경로가 존재한다. 따라서 일시적인 무효화에 복구 파일 생성까지 수행하는 처리가 불필요한 재작업을 늘리는 구조다.

최초 무효화를 발생시킨 개별 파일 이벤트는 이번 로그에서 식별되지 않았다. 특정 watcher가 최초 원인이라고 단정하지 않는다. 관측된 무효화·복구 파일 생성·추가 컴파일은 확인된 사실이며, 이들 사이의 세부 인과 시간은 추가 이벤트 식별로 보완할 수 있다.

관련 코드: `DataSystem.cpp`의 `ConfigureModelMaterialGraph`, `ConfigureEditableDefaultMaterialGraph`, `ApplyAssetChange`; [배치 재시도](../Editor/EngineEntry/EditorModelPlacement.cpp)의 `RetryInvalidatedPreparation`.

### 3 실제 화면 멈춤 후보는 렌더 스레드의 첫 메시 준비다

첫 실행의 frame 7313에서 다음 포함 관계가 기록됐다. 각 행은 중첩 범위이므로 합산하지 않는다.

| CPU 범위 | 시간 |
|---|---:|
| `RenderThreadFrame` | 2,209.991ms |
| `MaterialGraphScenePrepare` | 1,867.299ms |
| 그 안의 `MaterialSceneCasterSnapshot` | 1,240.069ms |
| 그 안의 한 `MaterialMeshGpuPrepare` | 328.605ms |
| 그 안의 `MeshClusterResidencyPrepare` | 311.356ms |

`MaterialSceneCasterSnapshot`은 이름보다 넓은 범위로 draw 준비와 메시·그림자 준비 등을 포함한다. 이를 전부 셰이더 컴파일이나 그림자 처리 비용으로 해석할 수 없다. `MeshClusterResidencyPrepare`는 렌더 준비 경로에서 `ClusterStream()->Prepare`를 동기 호출한다. 첫 cluster residency 준비가 긴 프레임의 구체적인 개선 대상이다.

재시작 실행에서도 렌더 스레드 최대 1,068ms 및 1,424ms, cluster residency 최대 약 318ms 및 300ms가 기록됐다. 반면 첫 실행의 `MaterialLookupPrepare` 최대는 약 181ms였다. 과거 lookup 셰이더 초기화 문제가 이번에도 최대 원인이라고 전제할 근거는 없다.

이 수치는 CPU 범위의 관측치이며 실제 모니터 표시 정지나 GPU 실행 시간이 아니다. 캡처 명령 전후에는 readback·관측 비용도 생길 수 있어 모든 긴 프레임을 모델의 정상 렌더 비용으로 묶지 않았다. 위 2.21초 프레임은 Robot 재배치 후 출력 확인 캡처 frame 7370보다 앞선 첫 렌더 구간이다.

관련 코드: [SceneHost 준비](../Engine/RenderEngine/MaterialGraphSceneHost.cpp), [메시 준비](../Engine/RenderEngine/MaterialGraphMeshSurface.cpp).

### 4 텍스처 준비는 두 번째 큰 CPU 로드 비용이다

Robot의 첫 `ModelGen.Load`는 7.786초였고 `ModelGen.Textures`가 7.742초였다. 이미지 decode 12회 합계는 약 6.960초, encoded 이미지 read 합계는 약 54ms였다. 따라서 이 표본에서는 원본 파일 읽기보다 decode를 포함한 이미지 준비가 컸다. decode 범위 내부의 mip·포맷 변환·압축 비율까지는 분해하지 않았다.

Robot의 GLB 43.45MB 중 embedded image는 38.39MB다. Aria는 GLB 10.88MB 중 image 7.98MB다. Robot 이미지 12개의 생성된 decoded cache는 합계 약 268.44MB였다. PNG 등 압축 원본의 크기만으로 런타임 메모리 및 이미지 준비 비용을 예상하면 크게 어긋난다.

## 최적화 우선순위

| 순서 | 변경 방안 | 목적과 검증 기준 |
|---|---|---|
| 1 | 재질 준비의 `invalidated`, `superseded`, `pending`을 실제 문서·컴파일 오류와 구분한다. 일시적 상태는 요청 재시도 또는 이전 정상 generation 유지로 처리한다. | 변경 없는 재시작에서 새 `Recovered` 파일 0개, 무효화로 인한 복구 컴파일 0회. 실제 잘못된 재질의 편집 가능한 복구 기능은 유지한다. |
| 2 | 캐시 거절 사유를 구조화하고, 같은 입력·compiler·backend·의존 digest의 검증된 제품을 재사용한다. | 두 모델의 변경 없는 재시작에서 full material compile 0회. 관련 shader/include 변경 시 필요한 제품만 정확히 재생성한다. 검증 조건을 삭제해 적중률만 올리지 않는다. |
| 3 | 모델 재질을 배치 요청 전에 준비한다. 여러 재질은 공유 요청으로 중복을 제거하고 제한된 compiler lane에서 병렬 준비한다. | 배치 임계 경로에서 4개·3개의 순차 컴파일 제거. 실제 컴파일 동시성·메모리를 측정해 제한하고, 무효화 시 낡은 결과 게시를 차단한다. |
| 4 | 첫 cluster residency의 파일 처리·변환을 준비 작업으로 옮기고 업로드/페이지 반영에 프레임별 예산을 둔다. | `MeshClusterResidencyPrepare`의 약 300ms 단일 호출 제거. 준비 중 이전 정상 화면을 유지하고, 메시·재질·텍스처가 준비된 뒤 일관된 표시로 전환한다. |
| 5 | GPU가 바로 소비할 수 있는 mip·압축 제품과 검증된 이미지 캐시를 활용하고 immutable pixel backing을 공유한다. | Robot의 약 7.7초 이미지 준비 및 약 268MB decoded backing을 각각 줄이는지 측정한다. 색 공간, normal·ORM, mip 품질은 유지한다. |
| 6 | 코드가 동일한 PBR 재질의 shader 제품을 정규화해 공유하고 값·텍스처·편집 identity는 instance 데이터로 분리한다. | 재질 수만큼 동일 구조를 컴파일하는 비용 감소. graph 의미·layout·backend·compiler 키가 동일한 경우에만 공유한다. |
| 7 | 진행 상태를 자산 준비, 재질 준비, 씬 적용, 렌더 준비로 분리한다. | `model.async ready` 이후에도 재질 프로그램 대기로 출력 실패했던 상태를 완료로 오인하지 않게 한다. |

1~3은 긴 대기 시간을, 4는 첫 화면의 긴 렌더 구간을 우선 겨냥한다. 본 수 감소나 씬 엔티티 생성 최적화는 현재 수십 초 병목에 대한 첫 선택이 아니다. 개선 후 예상 초수나 배속은 실제 변경 전후 측정 없이 약속하지 않는다.

재질 병렬화는 현재의 직렬화만 해제하는 변경으로 끝내면 안 된다. 공유 재질 저장소·게시·무효화의 스레드 안전성을 확인하고, immutable 준비 결과와 scene owner 게시를 분리한 다음 적용해야 한다.

## 계측 신뢰 범위

세 녹화는 모두 파일 끝까지 finalized됐고 unacked stream 0, dropped frame/event/counter 0이었다. 다만 늦게 도착한 CPU event가 각각 7개, 3개, 2개 있어 `complete=false`다. 전체 이벤트가 손실 없이 수집됐다는 수용 판정, 프레임 p99 비교나 성능 향상 비율의 근거로 사용하지 않는다. 위 마커 시간은 저장된 완결 범위의 실제 관측치이며 전체 실행의 최대치나 모든 비용의 완전한 분해를 보장하지 않는다. 벽시계 배치 로그와 성공한 렌더 캡처는 별도의 근거다.

첫 실행은 60초 대기 timeout과 Aria 첫 출력의 `material program is still preparing`로 종료 코드 4였다. 이후 두 실행은 종료 코드 0, 실패한 명령 0개였다. 첫 실패를 성공 표본에 섞지 않았다. 배치 큐가 비었다는 것만으로 정상 화면 표시를 보장하지 않는 점도 확인됐다.

이번 변경은 계측 코드 추가이며 위 최적화를 구현한 결과가 아니다. 원본 모델 및 사용자 씬을 저장해 변경하지 않았다. 정상 임포트 과정에서 모델 sidecar, generated material, 캐시와 복구 재질이 생성됐다.

## 재현과 근거 파일

저장소 루트에서 다음을 실행하면 같은 카메라·조명·모델 순서로 측정한다. 결과는 별도 실행 이름의 디렉터리에 저장한다.

```powershell
pwsh -NoProfile -File Build/model-load-20261021/measure.ps1 -RunName next-robot-first
pwsh -NoProfile -File Build/model-load-20261021/measure.ps1 -RunName next-aria-first -Reverse
```

- [측정 결과 통합 JSON](../Build/model-load-20261021/evidence.json)
- [모델 구조](../Build/model-load-20261021/assets.json), [장비](../Build/model-load-20261021/machine.json), [실행 identity](../Build/model-load-20261021/identity.json)
- [첫 실행 로그](../Build/model-load-20261021/robot-first-1/stdout.txt), [Aria 우선 재시작 로그](../Build/model-load-20261021/aria-first-2/stdout.txt), [Robot 우선 재시작 로그](../Build/model-load-20261021/robot-first-3/stdout.txt)
- 각 실행 디렉터리의 `results.jsonl`, `execution.json`, `capture.ceprof`, `capture.ceprof.summary.json`, 모델별 `manifest.json`과 `depth.f32`.
- [첫 긴 렌더 프레임의 중첩 범위](../Build/model-load-20261021/robot-first-1/capture.slow.json)
- `Build/model-load-build-20261021.log`: 이번 재빌드 로그.

`Build` 아래의 원시 계측 결과는 로컬 산출물이며 Git 추적 대상이 아니다. 실행 파일 launcher의 SHA만으로 소스 일치를 판정하지 않도록 실제 `CreatorEditor.runtime.dll`과 계측 소스의 hash를 `identity.json`에 함께 남긴다.
