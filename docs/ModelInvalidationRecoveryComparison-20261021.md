# 모델 무효화 복구 조건 수정과 재계측 비교

2026년 10월 21일

일시적인 준비 무효화를 재질 손상으로 처리해 새 복구 재질을 생성하던 조건을 수정했다. Debug x64 Editor를 다시 빌드하고 CreatorRobot과 Aria를 실제 DX12 씬에 배치해 확인했다. **새 `Recovered` 재질은 두 최종 실행 모두 0개**였고, 각 실행의 첫 배치·반복 배치 4회가 모두 완료됐다.

재시작 후 첫 배치는 CreatorRobot **23.184초 → 20.182초**, Aria **21.879초 → 13.338초**였다. 각각 약 13%, 39% 짧아진 표본이다. 무효화 재시도는 여전히 남아 있고 두 번째 모델과 반복 배치는 일관되게 빨라지지 않았다. 이번 수정은 불필요한 복구 생성을 제거한 단계이며 모델 로드 전체의 최적화 완료를 의미하지 않는다.

## 수정 내용

1. `GenerationStore::Publish`가 이전 준비 요청의 게시를 거부했을 때, 오류 문자열과 별도로 무효화 여부를 전달한다. 정상 게시 시에는 이 상태를 해제한다. 낡은 generation의 게시 거부는 유지한다.
2. `LoadMaterialGraphGeneration`은 재질 하나의 요청만 교체됐을 때 최신 요청에 최대 3번 다시 합류한다. 프로젝트 전체의 준비 epoch가 변경되거나 종료·무효화가 진행 중이면 이전 결과를 게시하지 않는다.
3. `ConfigureModelMaterialGraph`는 무효화 상태와 준비 epoch·resolver revision을 확인한다. 일시적인 무효화에는 복구 `.shadergraph` 생성과 기본 재질 컴파일을 수행하지 않는다. 안정된 원본의 실제 오류에 대한 복구 분기는 유지한다. 취득한 generation을 재사용해 설정 중 다시 generation을 조회하는 경로도 없앴다.
4. 배치의 재시도는 100ms 간격으로 다음 프레임에서 재입장을 시도한다. 첫 무효화 감지 후 30초까지만 허용하며 장면·프로젝트 변경, 취소, 이미 시작된 엔티티 게시 검사는 유지한다. 일시적으로 입장을 거절당해도 바로 최종 실패로 처리하지 않는다. 게임 스레드를 잠들게 하거나 완료되지 않은 작업을 기다리지 않는다.

4번은 실제 재계측에서 필요성이 확인됐다. 복구 생성만 막은 초기 수정본은 첫 Robot 요청이 기존 3회 한도를 소진해 실패했다. 이 결과(`fixed-robot-first-1`)는 개선 표본에서 제외했다. 최종 수정본은 같은 상황에서 7회의 준비 재시도를 거쳐 정상 배치됐다. 30초는 무효화 이후 재시도 구간의 한도이며 모델 전체 로드 시간의 강제 한도가 아니다.

관련 코드: [DataSystem](../Engine/RenderEngine/DataSystem.cpp), [generation 게시](../Engine/RenderEngine/MaterialGraphRuntime.cpp), [배치 재시도](../Editor/EngineEntry/EditorModelPlacement.cpp).

## 비교 조건

- 기존 [기준선 보고서](ModelLoadingProfile-20261021.md)의 성공한 재시작 실행 두 건을 비교에 사용했다. 최초 cold 성격의 Robot timeout 및 Aria 53초 표본과 직접 비교하지 않았다.
- 동일한 i7-8700K, RTX 2080 Ti, Debug x64, DX12 Debug Layer on, GPU Validation off 조건이다. 같은 `Dynamic_CPP` 프로젝트와 원본 GLB를 사용했다. 디스크·OS·shader 캐시는 지우지 않았으므로 완전히 초기화한 A/B 벤치마크가 아니다.
- 각 프로세스에서 카메라·조명을 만들고 모델 두 개를 차례로 배치한 다음 같은 순서로 다시 배치했다. 첫 모델을 Robot과 Aria로 바꿔 각각 실행했다. 최종 비교 실행 중 별도 빌드·프로파일 분석 작업은 수행하지 않았다.
- 측정값은 요청 생성부터 **씬 적용 완료**까지의 벽시계 시간이다. 사용자가 보는 첫 정상 화면까지의 지연과 구분한다. 표시 확인은 332×144의 실제 DX12 출력 캡처로 별도 검증했다.
- 표의 변화율은 해당 실행 쌍의 관측 차이다. 반복 표본의 통계적 안정성이나 일반적인 향상률을 확정한 수치가 아니다.
- 프로파일 분석과 시작 구간이 겹친 `fixed-aria-first-3`은 비교에서 제외하고, 별도 작업 없이 실행한 `fixed-aria-first-4`를 사용했다.

## 배치 시간 비교

| 순서와 모델 | 수정 전 | 수정 후 | 표본 차이 |
|---|---:|---:|---:|
| Robot 우선 실행의 첫 Robot | 23.184초 | 20.182초 | 12.95% 단축 |
| 같은 실행의 두 번째 Aria | 3.274초 | 3.971초 | 21.29% 증가 |
| Aria 우선 실행의 첫 Aria | 21.879초 | 13.338초 | 39.04% 단축 |
| 같은 실행의 두 번째 Robot | 4.953초 | 4.669초 | 5.73% 단축 |
| 같은 프로세스 Robot 재배치 범위 | 0.985~1.002초 | 1.003~1.184초 | 개선 확인 안 됨 |
| 같은 프로세스 Aria 재배치 범위 | 0.255~0.256초 | 0.256~0.267초 | 사실상 동일한 수준 |

첫 모델의 긴 대기는 줄었지만 전체 요청이 일괄적으로 빨라진 결과는 아니다. 특히 Robot 우선 실행의 Aria 첫 배치는 느려진 표본이므로 제외하지 않고 표에 포함했다. 충분한 반복 없는 이 차이를 안정적인 성능 회귀 또는 향상으로 단정하지 않는다.

## 복구 생성과 재시도 비교

| 항목 | Robot 우선 수정 전 → 후 | Aria 우선 수정 전 → 후 |
|---|---:|---:|
| 새 `Recovered` 재질 | 1개 → 0개 | 1개 → 0개 |
| 첫 모델의 배치 준비 재시도 | 2회 → 7회 | 2회 → 5회 |
| 기록된 최장 단일 `Material.CompileSceneProduct` | 15.352초 → 3.036초 | 15.902초 → 0.987초 |
| 최종 배치 상태 | 4완료·0실패 → 4완료·0실패 | 4완료·0실패 → 4완료·0실패 |

수정 후에도 10회 및 15회의 재질 컴파일 범위가 기록됐다. 따라서 “복구 재질 생성 0개”를 “재질 컴파일 0회”로 해석하면 안 된다. 무효화로 다시 준비하는 비용과 기존 재질 제품 준비 비용은 남아 있다. Aria 첫 요청의 마지막 `prepareMs`는 약 4ms였지만 전체 요청은 13.338초였다. 마지막 시도 시간만으로 전체 대기를 설명할 수 없다.

다음 우선순위는 무효화 이벤트의 원본 경로·GUID·종류·epoch를 연결해 반복 재시도의 생산자를 확인하고, 내용이 동일한 자체 저장·중복 알림을 합칠 수 있는지 검증하는 것이다. 실제 변경이나 resolver 교체를 무시하는 방식으로 재시도를 줄여서는 안 된다.

## 표시와 계측 검증

- 최종 두 실행 모두 Editor 종료 코드 0, 실패한 명령 0개, 배치 4완료·0실패다. 대기 명령이 성공했는지만 보지 않고 실제 완료 수와 렌더 출력도 검사했다.
- 각 실행에서 Robot의 첫 배치·재배치 모두 4 draws와 깊이 픽셀 324개, Aria는 모두 3 draws와 깊이 픽셀 525개를 확인했다. 캡처는 모두 finite다. 이는 기본 표시 확인이며 재질 외관 및 애니메이션 품질 검사는 아니다.
- 최종 두 `.ceprof` 모두 `complete=true`, unacked 0, dropped frame/event/counter 0, late CPU/GPU event 0이다. 기준선 두 녹화에는 late CPU event가 2개·3개 있어 전체 마커 합계나 프레임 p99의 정밀한 전후 성능 판정은 하지 않았다. 위 배치 시간은 별도 벽시계 로그를 사용했다.
- 수정 후에도 렌더 스레드 최대 약 1.724초·0.936초, cluster residency 준비 최대 약 354ms·291ms가 관측됐다. 이번에는 해당 코드를 최적화하지 않았으며 첫 화면 멈춤 문제가 해결됐다고 판단하지 않는다.
- 별도 generation 수명 probe는 무효화된 ticket 거절, 최신 준비 게시 성공, 성공 시 무효화 상태 해제, 실제 loader 오류 보존, 이전 정상 owner 유지 검사를 통과했다. [실행 로그](../Build/model-load-20261021/generation-probe.log).
- 최종 Editor에서 준비 중인 Robot의 Undo 취소와 Aria의 씬 전환 취소를 실행했다. pending 0·failed 0·cancelled 2였고, 저장한 두 시험 씬에 모델 엔티티·mesh가 남지 않았다. [취소 검사 결과](../Build/model-load-20261021/cancel-final/summary.json).
- 기존 전체 `verify-material-product.ps1`은 현행 `CookedProgramVersion=13`과 맞지 않는 version 5 고정 assertion 때문에 실행 단계에 도달하지 못했다. 이 전체 검사가 통과했다고 주장하지 않는다. 수정한 수명 검사와 동등한 핵심 조건은 위 별도 probe 및 실제 두 모델 배치로 확인했다.

실제 손상된 그래프에 대해서는 원본을 보존하면서 별도 복구 재질을 생성하는 분기가 실행되는 것을 확인했다. 다만 별도 손상 fixture의 비동기 배치는 **수정 전 바이너리와 최종 바이너리 모두 실패**했고, 씬 mesh 0개·큐 failed 1개였다. 최종 바이너리의 프로세스 종료 코드는 0이었지만 큐 실패와 출력 부재를 기준으로 실패 판정했다. 이 기존 실패 시나리오의 완전한 복구는 이번 수정 범위에서 해결하지 않았다. 정상 CreatorRobot·Aria 표본에는 손상 그래프를 주입하지 않았다. 관련 확인 결과는 [손상 fixture 비교](../Build/model-load-20261021/corrupt-comparison.json)에 보존한다. 비교 종료 후 최종 runtime을 복원하고 SHA256 일치를 확인했다.

## 실행 근거

- [전후 통합 데이터와 전체 비교표](../Build/model-load-20261021/recovery-comparison.json)
- [최종 runtime 및 소스 identity](../Build/model-load-20261021/recovery-identity.json)
- [Robot 우선 최종 로그](../Build/model-load-20261021/fixed-robot-first-2/stdout.txt), [Aria 우선 최종 로그](../Build/model-load-20261021/fixed-aria-first-4/stdout.txt)
- 각 최종 실행 디렉터리의 `results.jsonl`, `execution.json`, `capture.ceprof`, `capture.ceprof.summary.json`, 모델별 `manifest.json`과 `depth.f32`.
- `Build/model-load-recovery-build-20261021.log` 및 `Build/model-load-recovery-build2-20261021.log`: 수정과 재시도 보완 후 빌드 로그. 모두 빌드 종료 코드 0.
- 이전 실제 runtime SHA256: `D118CBA4D44378145155EAD95A963B20B3ADA2C613FFFD81029C6A9C41C377CD`
- 최종 실제 runtime SHA256: `F82A15157EF341C8171C291552989EF93FDE8C1E1C77EBE9F92A98C1393E0B72`

원본 GLB와 사용자 씬은 변경하지 않았다. 시험용 씬과 손상 fixture는 `Build/model-load-20261021`에만 저장한다. 기존 캐시와 사용자의 다른 변경은 유지한다. `Build` 아래 근거 파일은 로컬 산출물이며 Git 추적 대상이 아니다.
