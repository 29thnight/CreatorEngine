# Commandlet 정리 결과 — 2026-09-06

기준 HEAD `55374f16`의 미커밋 작업 트리에 적용했다. 현재 런타임 discovery는
**제품 명령 98개(이름 105개), Commandlet 103개**다. 이번 후속 정리 전의
95/111에서 제품 진단·태그 기능을 옮기고 종료된 비교 벤치를 제거했다.
[처분표](CommandSurfaceDisposition.tsv)는 실제 이름 집합과 대조했으며,
[전체 구조](CommandSurfaceImplementation.md)에 앞선 CLI/Commandlet 분리 기록이 있다.

## 이번 마무리 반영

`assets.scenemodel`을 Commandlet으로 이동했고 사용하지 않는 `animator.state/exit`를 제거했다.
제품 98개 모두 결과를 반환하며 named JSON 입력 58개·Undo 선언 21개다.
`animator.param`·`render.matmode`는 GUI 공통 Undo 경로를 사용한다.
미보고 상태와 void/직접 exit 어댑터, 한국어 verdict 소비자 13개 및 명령 소스 존재 소비자 6개를 정리했다.
최신 실행 결과 및 전체 종결을 보류하는 검증 경계는 [Phase14_5Closure.md](Phase14_5Closure.md)를 본다.

## 제거한 명령과 코드

| 제거한 Commandlet | 종료 근거와 실제 제거 범위 |
|---|---|
| `dx12.bench11` | 완료된 DX11/DX12 API 비용 비교. 벤치 구현·등록·빌드 항목 제거. |
| `dx12.encoderbench` | R3/3-16 encoder 선택을 위한 비교 종료. 구현·등록·빌드 항목 제거. |
| `dx12.ssaoscale` | 완료된 3-6 알고리즘 시간 비교. 벤치와 제품 SSAO의 옛 참조 경로·PSO·셰이더 제거. |
| `dx12.postscale` | 완료된 Uber/분리 패스 시간 비교. 벤치 제거, `vk.post`가 쓰는 분리 패스 보존. |
| `dx12.forwardscale` | 완료된 Forward+ 광원 수 경계 측정. 벤치 제거, `dx12.forwardshade`의 출력 동등성 비교 보존. |
| `perf.reflect` | 완료된 CT7 측정. 명령과 golden 소비자의 측정 전용 프리팹 준비·반복 생성·파서 제거. |

벤치 C++ 5개, 전용 HLSL 4개와 프로젝트 등록을 제거했다. SSAO는 참조 셰이더의
컴파일/PSO 준비에 더 이상 의존하지 않는다. `reflect.golden`의 실제 타입 직렬화
대조는 유지한다. 과거 성능 수치로 현재 성능 향상을 주장하지 않는다.
종료 판단은 `RhiBoundaryPlan.md` R3, `RefactoringPlanDashboard.html` 3-6/3-16,
`ReflectionRedesignPlan.md` CT7에 근거한다.

## 제품 기능으로 이전

| 기존 Commandlet | 현재 제품 명령 | 공통 구현 |
|---|---|---|
| `experiment.animlive` | `animator.status` | `EditorDiagnostics::AnimatorStatus`; Animator 목록·팔레트 digest·제품 publication 지표를 JSON으로 반환. |
| `scene.hierarchycheck` | `scene.hierarchycheck` | `EditorDiagnostics::ValidateHierarchy`; 활성 씬의 계층 불변식 검사. |
| `tag.authoring.probe` | `tag.list/has/add/remove` | `EditorProjectOperations`; Inspector와 동일한 태그 편집·저장·Undo/Redo. |

구 이름 두 개는 별칭으로 남기지 않았다. HTTP discovery에서 입력 스키마와 결과를
조회할 수 있다. 태그 변경은 성공 즉시 저장하며 Undo/Redo도 디스크에 반영한다.
동일 값은 이력을 늘리지 않고, 예약 태그·잘못된 이름·현재 사용 중인 태그 삭제를
거부한다. 저장 실패는 메모리 정의를 복구하고 성공으로 보고하지 않는다.
Animator 진단은 제품이 게시한 스냅샷을 읽으며 pose를 다시 게시하지 않는다.

## 숨은 에셋 선택 제거

| 명령 | 명시적 입력 |
|---|---|
| `experiment.foliage` | `seed <asset-directory> <model-path>` 또는 `verify` |
| `scene.transformbulk` | `probe <pose-model-path> <rebind-model-path>` |
| `dx12.selftest` | `<texture-path> [output]` |
| `vk.selftest` | `<model-path> <texture-path> [output]` |
| `rhi.uploadsegments` | `<model-path> <texture-path>` |
| `experiment.matruntime` | `seed <asset-directory> <texture-path>`, `edit` 또는 현재 상태 검사 |

Gunner/SU/scene.glb/Cube 텍스처를 명령 본문이 임의로 선택하던 부분을 입력으로 바꿨다.
필요한 조건은 유지한다. bulk 검사는 서로 다른 skeleton 모델과 최소 2개 bone을,
upload 검사는 16 MiB를 넘는 실제 mesh 업로드 입력을 요구한다. 재질 seed의 결과도
성공·실패·입력 오류를 반환하며 검사할 재질이 없으면 통과로 처리하지 않는다.

소비 PowerShell은 모델·씬·텍스처 경로를 인자로 받는다. typed consumer의
`ExpectedPoseDigest`는 독립 기준이며, 다른 모델/씬을 지정하면 반드시 함께
제공해야 한다. 실행 결과를 새로운 기대값으로 자동 채우지 않는다.

**의도적으로 남긴 코퍼스 의존성:** `assets.generationcorpus`는 Gunner/SU의
메시·재질·텍스처·애니메이션 수를, `assets.scenemodel`은 Gunner를 포함한 씬에서
embedded texture 수를 추가로 대조한다. 특정 그림의 coverage·pose digest를 재는
시각 회귀 스크립트도 전용 코퍼스 검사다. 이들은 HTTP 제품 명령이 아니며,
임의의 프로젝트에서 입력 없이 실행 가능한 검사로 간주하지 않는다.
기본 셰이더/자가 검사 셰이더는 엔진 계약을 검증하는 내장 fixture로 남는다.

## 이전 101/108 표면의 검증 이력과 한계

- Debug/Release Editor, Debug Player 빌드 통과. 기존 LNK4229 경고는 남는다.
- Debug/Release `verify-editor-command-surface.ps1` 각각 **230개 단정 통과**.
  삭제 명령 부재, 입력 누락 거부, HTTP 진단, 태그 저장·Undo/Redo·정의 순서를 검사했다.
- Debug/Release 재질 fixture 입력 집중 검증 각각 **3개 통과**: 입력 누락 거부,
  검사 대상 부재 거부, 명시적 텍스처로 자산 게시. 임시 자산은 제거했다.
- Debug/Release 제품 registry golden **101/108, problems 0** 일치.
  처분표의 97 api + 3 split + 1 batch, Commandlet 102, 제거 12가 discovery와 일치한다.
- reflection golden **75개 타입 diff 0**, 태그 read **21개/레이어 16개** 및 원본 보존,
  hierarchy mutation 회귀 통과. MBC freeze 정적 검사와 CLI consumer 계약 검사도 통과.
- `dx12.ssao`, `dx12.post`, `dx12.forwardshade`, `vk.post` 통과.
  `vk.post`는 Uber/분리 패스의 DX12/Vulkan 픽셀 비교를 포함하며 validation 0건이다.
- `dx12.selftest`는 **실패가 남는다**. 앞선 단계부터 발생한 예외를 이번에
  `material authoring round trip` 단계로 좁혔다. 텍스처 입력 처리 이전이다.
  이제 누적 로그와 실패 단계가 JSON `data.log`에 남고 suite는 실패 종료한다.
  재질 저장·복원 예외의 근본 원인이 해결됐다는 뜻은 아니다.
- 모델 typed consumer, transform bulk, skinned proxy의 긍정 회귀는
  작업 폴더에 `Gunner_F_Mythic.glb`/`SU_Mythic.glb`가 없어 완료하지 못했다.
  `vk.selftest`/`rhi.uploadsegments`의 유효 대형 모델 입력 검증도 미확인이다.
  누락된 코퍼스를 통과나 미구현으로 바꾸지 않는다.
- 전체 102개 Commandlet의 모든 모드·코퍼스를 실행한 결과는 아니다.
  처분표의 다른 일반적 유지 문구도 개별 종료 검토가 완료됐다는 증거가 아니다.

로그는 `artifacts/commandlet-cleanup/`에 있다. Debug/Release의 surface 결과,
`debug-gates/render/verdicts.csv`, `vulkan-post/results.jsonl`,
`debug-selftest-diagnostics/dx12.selftest.result.jsonl`,
`material-input-debug.log`/`material-input-release.log`로 판정 근거를 확인할 수 있다.
