# 모델 준비 무효화 반복 원인 수정 및 재계측

2026-10-21, CreatorRobot / Aria_Dagger_ChainSwordWeapon, Debug x64 Editor, 실제 DX12 실행.

이번 수정 후 두 모델의 시작 순서를 바꾼 실행에서 **총 8회 배치가 모두 완료됐고, 준비 무효화 재시도와 새 Recovered 재질은 모두 0회/0개**였다. 다만 시작 시 텍스처 등록 대기 약 9초와 첫 표시 시 렌더 스레드의 1초 이상 준비 비용은 남아 있다. 로드 지연 전체가 해결된 상태는 아니다.

## 반복 원인과 실행 증거

모델이나 재질 원본의 반복 변경이 원인이 아니었다. EditorAssetDatabase가 시작 시 기존 텍스처도 비동기로 확인·가져오기하고, 각각의 결과를 PublishAuthoredTexture → MountAssetSet으로 등록한다. 이 경로는 카탈로그 revision을 올리며 전체 모델·재질 캐시와 진행 중인 준비를 무효화한다. DetachLegacyCachesLocked는 재질 그래프 generation 저장소도 비우므로 진행 중인 그래프 게시가 거절되고 다시 컴파일되는 비용까지 발생한다.

수정 전 동작에 추적만 추가한 `trace-before-robot`에서 텍스처 게시 43건이 기록됐다. Robot 요청의 첫 ticket은 resolver revision 4였고, 그 뒤 39번의 카탈로그 갱신이 남아 있었다. 모델 준비 epoch는 1로 유지됐다.

```text
ticket epoch=1->1 resolver=4->5   cancelled=1
ticket epoch=1->1 resolver=5->31  cancelled=1
ticket epoch=1->1 resolver=31->39 cancelled=1
ticket epoch=1->1 resolver=39->43 cancelled=1
```

이 실행의 Robot은 4회 재시도했고 14,884.922ms에 완료됐다. 같은 구간에서 graph-publish의 current=0도 기록되어 저장소 전체 분리로 진행 중인 게시가 거절되는 경로가 확인됐다. 기존 이전 계측에서는 시작 작업과의 겹침에 따라 Robot 7회, Aria 5회까지 재시도했다.

관련 위치:

- [텍스처 시작 등록](../Editor/EngineEntry/EditorAssetDatabase.cpp), ScanAndRegisterMeta / QueueTextureReimportLocked / CookTextureGeneration.
- [카탈로그 게시](../Engine/RenderEngine/AssetDepot/AssetDepot.cpp), PublishAuthoredTexture / MountAssetSet.
- [준비 유효성 검사 및 전체 캐시 분리](../Engine/RenderEngine/DataSystem.cpp), IsModelPreparationInvalidated / DetachLegacyCachesLocked.

## 수정한 동작

카탈로그 revision 검사나 실제 변경 시 무효화를 삭제하지 않았다. 여러 typed asset과 게시 경로가 이 검사에 의존하므로 이번에는 Editor 배치의 작업 시작 순서를 수정했다.

1. EditorAssetDatabase에 진행 중인 텍스처 cook 또는 프레임 경계 게시 대기 여부를 읽는 HasPendingTexturePublications를 추가했다. Loading 상태의 이미지 준비/GPU 작업까지 기다리지는 않는다. 실패한 dispatch/import는 대기를 유지하지 않는다.
2. 배치 요청은 즉시 등록해 진행 UI와 취소·undo 대상이 되지만, 알려진 텍스처 카탈로그 게시 작업이 남아 있으면 모델 준비를 아직 제출하지 않는다. 이후 프레임에서 상태만 확인한다. 게임 스레드에서 sleep하거나 완료 작업을 기다리지 않는다.
3. 이미 무효화된 요청도 진행 중인 게시가 끝나기 전에는 새 준비를 반복 제출하지 않는다. 기존 30초 재입장 한도와 새 변경 시 유효성 검사는 유지한다.
4. 작업 제출 전에도 씬·게임 모드·프로젝트·자산 루트가 같은지 확인한다. 대기 중인 요청은 준비 목록에 남겨 취소와 종료 정리가 적용되게 했다.
5. 요청 시간 로그에 admissionWaitMs를 추가했다. 총 로드 시간에는 이 대기도 포함한다. CE_TRACE_ASSET_INVALIDATION 환경변수로 ticket의 epoch/revision, 자산 변경 원인, 거절된 그래프 게시를 추적할 수 있다.

이 변경은 전체 카탈로그 무효화의 범위를 줄이는 구현은 아니다. 기존 안전장치를 유지하면서, 이미 알려진 게시 작업과 비싼 모델 준비가 충돌하는 반복을 제거한다. 준비 시작 뒤 새 파일 변경이 생기면 여전히 무효화와 재시도가 필요하다.

## 비교 조건

- 같은 프로젝트, 원본 GLB, i7-8700K / RTX 2080 Ti, Debug x64, DX12 Debug Layer on, GPU Validation off 조건이다.
- 프로세스마다 새 씬·카메라·조명을 만들고 두 모델을 순차 배치한 다음 같은 순서로 재배치했다. Robot 우선과 Aria 우선을 각각 실행했다.
- 수치는 요청 생성부터 씬 적용 완료까지의 벽시계 시간이다. 첫 정상 화면까지의 시간과 구분한다. 실제 렌더 출력은 별도로 검증했다.
- 파일/OS/shader 캐시를 지우지 않았다. 각 순서 1회씩의 표본이며 통계적으로 안정된 향상률을 뜻하지 않는다. 배치 성능 실행 중 별도 빌드나 프로파일 분석은 실행하지 않았다.
- 이전 기준선은 직전 복구 조건 수정 후 성공 실행인 fixed-robot-first-2 / fixed-aria-first-4다. 이번에는 수정 전 추적 실행도 추가했다. 새 기준선 Robot이 이미 20.182초에서 14.885초로 달라져 시작 작업의 변동이 큼을 확인했다.

## 배치 시간 전후

| 실행 순서 / 모델 | 직전 기준선 | 이번 수정 후 | 재시도 전 → 후 |
|---|---:|---:|---:|
| Robot 우선, 첫 Robot | 20.182초 | 14.078초 | 7 → 0 |
| 같은 실행, 두 번째 Aria | 3.971초 | 3.374초 | 0 → 0 |
| 같은 실행, Robot 재배치 | 1.184초 | 1.068초 | 0 → 0 |
| 같은 실행, Aria 재배치 | 0.267초 | 0.272초 | 0 → 0 |
| Aria 우선, 첫 Aria | 13.338초 | 12.465초 | 5 → 0 |
| 같은 실행, 두 번째 Robot | 4.669초 | 5.301초 | 0 → 0 |
| 같은 실행, Aria 재배치 | 0.256초 | 0.271초 | 0 → 0 |
| 같은 실행, Robot 재배치 | 1.003초 | 1.053초 | 0 → 0 |

추가한 수정 전 추적 실행과 같은 순서로 비교하면 첫 Robot은 **14.885초 → 14.078초, 약 5.4% 단축**, 재시도는 **4 → 0**이다. 직전 기준선 대비 Robot의 약 30% 차이를 모두 이번 수정의 효과로 해석하면 안 된다. 첫 Aria는 직전 기준선 대비 약 6.5% 짧았다. 두 번째 Robot은 오히려 약 13.5% 길었고 재배치는 개선이 확인되지 않았다.

수정 후 첫 Robot의 admissionWaitMs는 9,050.991ms, 첫 Aria는 8,934.786ms였다. 등록 대기를 숨기거나 총 시간에서 제외하지 않았다. 대기 이후 recipe 준비는 Robot 4,076.194ms, Aria 3,044.581ms였다. 반복 배치에는 admission 대기가 대체로 한 프레임(14~17ms) 추가된다.

## 컴파일과 멈춤 구간

| CPU 프로파일 항목 | Robot 우선 전 → 후 | Aria 우선 전 → 후 |
|---|---:|---:|
| Material.CompileSceneProduct 호출 수 | 10 → 7 | 15 → 7 |
| 위 범위의 최장 단일 호출 | 3,035.849 → 947.496ms | 987.436 → 1,003.663ms |
| ModelPlacement.Tick 최장 호출 | 29.765 → 13.017ms | 12.311 → 3.054ms |
| RenderThreadFrame 최장 CPU 범위 | 1,723.936 → 1,550.473ms | 936.399 → 1,045.304ms |

새 실행은 각각 모델 재질 수의 합인 7회의 컴파일 범위를 기록했다. 불필요한 반복은 줄었지만 컴파일 자체가 없어진 것은 아니다. 프로파일은 두 실행 모두 complete=true, unacked=0, droppedEvents=0이다.

렌더 스레드의 긴 구간은 남았다. Robot 우선 실행의 최장 프레임에는 MaterialGraphScenePrepare 1,234.351ms, 그 안에 MaterialSceneCasterSnapshot 989.590ms가 기록됐다. Aria 우선 실행에서는 각각 780.110ms / 531.719ms였다. MeshClusterResidencyPrepare도 각각 최대 프레임 내부에 309.054ms / 202.279ms 범위를 포함했다. 이 범위들은 중첩되므로 합산하면 안 된다. CPU 범위이며 GPU 실행 시간이나 화면에 표시된 FPS로 해석하지 않는다.

## 검증 결과

- Debug x64 Editor 최종 재빌드 성공. 로그: Build/model-invalidation-final-build-20261021.log.
- 두 최종 실행 모두 4완료·0실패·0취소, 명령 전부 성공, 새 Recovered 재질 0개. 각 실행에서 텍스처 게시 43건은 여전히 발생했고 모델 ticket 무효화 추적은 0건이었다.
- 8개 실제 DX12 캡처 모두 finite=true, 332×144. Robot은 draw 4개·깊이 유효 픽셀 324개, Aria는 draw 3개·525개였다.
- 대기 중 undo와 씬 전환: cancelled=2, completed=0, failed=0, 두 저장 씬에 모델 메시 유출 0개.
- 별도 프로젝트 복사본에서 모델 준비 도중 assets.model.reimport 실행: epoch 1→2, 무효화 재시도 1회 후 정상 완료, 메시 4개. 캐시가 준비된 재실행은 exit=0이며 모든 명령이 성공했다. 실제 변경 감지가 살아 있음을 확인했다.
- 그 별도 프로젝트의 첫 실행은 캐시가 없는 셰이더 준비 때문에 첫 model.async wait의 60초 제한을 넘겼다. 최종적으로 약 89초에 모델은 완료됐지만 exit=4 / timed_out 명령 1개였으므로 통과 표본이나 성능 비교에 포함하지 않았다. 실패 기록도 보존했다.
- 원본 GLB SHA256은 기존 기록과 일치한다. 계측으로 바뀐 active.workspace는 실행 전 파일로 복원했다. 기존 다른 변경은 유지했고 커밋하지 않았다.
- 실제 손상된 재질 그래프 복구의 기존 실패는 이번 검증 범위에서 해결한 것으로 주장하지 않는다. 이전 보고서의 한계가 유지된다. 저장소 전체의 기존 material product 테스트를 통과했다고 주장하지 않는다.

## 다음 최적화 우선순위

1. 시작 시 텍스처 43건의 확인·등록에 걸리는 약 9초를 우선 줄여야 한다. 검증된 기존 cooked generation의 카탈로그를 일괄 게시하는 경로를 검토하고, 불필요한 중간 resolver 교체와 preload 재시작 비용을 별도 계측한다. 실제 source/meta 변경 검증은 유지해야 한다.
2. 일반적인 카탈로그 변경에도 모델 준비가 계속 필요하다면, 모델·재질이 실제 참조하는 identity와 immutable definition을 비교해 무관한 변경을 구별하는 설계가 필요하다. revision 검사만 삭제하면 낡은 결과를 게시할 수 있으므로 이번 수정에서는 적용하지 않았다.
3. 실제 화면 멈춤에는 MaterialSceneCasterSnapshot / MaterialGraphScenePrepare와 메시 cluster residency 준비의 선행 준비·프레임 분산을 우선 검토한다. 이번 결과만으로 렌더 스레드 멈춤이 해결됐다고 할 수 없다.
4. 두 모델의 재질 제품 준비 3~4초도 남아 있다. 같은 소스·컴파일 설정·제품 식별자가 재시작 간 정확히 재사용되는지 확인한 뒤 캐시 수명과 중복 준비를 최적화한다.

## 증거 위치와 실행 식별자

원시 자료는 `Build/model-invalidation-20261021/`에 있다.

- `trace-before-robot/`: 수정 전 동작에 원인 추적만 추가한 실행.
- `fixed-robot-first/`, `fixed-aria-first/`: 최종 시간 로그, 명령 결과, 실행 해시, 실제 렌더 캡처, CEPROF 및 summary/slow 분석.
- `comparison.json`, `render-validation.json`: 비교 수치와 렌더 검증 요약.
- `cancel-final/`: 취소·씬 전환 검증.
- `reload-during-prepare/`, `reload-during-prepare-warm/`: 실제 변경에 대한 무효화 검증의 첫 실행 및 성공 재실행.
- `measure.ps1`, `check-cancel.ps1`, `check-reload.ps1`, `check-reload-warm.ps1`: 재현 도구.

직전 기준선 runtime SHA256: F82A15157EF341C8171C291552989EF93FDE8C1E1C77EBE9F92A98C1393E0B72.

이번 추적 기준선: E6352506413472F87115CCDE5DA3DF1655E412BE0E1CB599C0EF76FDCF2B309A.

최종 runtime: 04DB2DADDEA7C19F6AABE04C455E79E73278F0A124D38FEA89B9E522680FF8B5.

HEAD는 3531f1aa201f4d9e0db97c39442d2cd0bdd8b0cc이며 기존 작업을 포함한 미커밋 상태로 빌드했다. 이전 단계는 [무효화 복구 조건 보고서](ModelInvalidationRecoveryComparison-20261021.md)를 참조한다.
