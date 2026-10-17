# GCCE 추가 검증 — 2026-10-09

Windows x64 Debug/Release 빌드, 구성별 Scene GC 20회 반복, 구성별 Play→Stop 회귀를 통과했다.
이 결과는 아래 테스트 범위의 객체 수명·직렬화·재생 복원 증거다. 성능 개선이나 전체 GC 수용 완료를 뜻하지 않는다.

## 소스와 실행 경로

- 기준 HEAD: `2965dc48` 및 이 검증의 로컬 테스트 변경.
- 기존 `RunSceneGCSelfTest`는 호출자가 없었다. `scene.gc.selftest`를 process-scoped commandlet로 연결했다.
  일반 편집/HTTP 명령 표에는 노출하지 않는다. 기존 GameThread의 scene borrow fence 안에서 호출한다.
- 마지막 순환 그래프 회수를 `collect_step(250μs, min_units=16)`으로 검사하고,
  수집 종료·idle 복귀·중단 횟수 불변을 추가 검증했다. pin 유지와 DDOL 이송 중에는 기존 full collection 검사를 유지한다.
- `Tools/regression/verify-scene-gc.ps1`는 새 호스트 DLL 여부, 성공 표지의 정확한 횟수,
  실패 표지 부재, 종료 코드 0을 검사한다. 별도 작업공간·파일 로그·시간 제한을 사용한다.

## 결과

| 검사 | Debug | Release | 근거 |
|---|---|---|---|
| Editor 및 종속 프로젝트 Build | exit 0 | exit 0 | `Artifacts/gc-validation-20261009/build-{debug,release}.log` |
| Scene GC 반복 | 20/20, exit 0 | 20/20, exit 0 | 구성별 `scene-gc.out.log`, `gc-results.json` |
| 마지막 점진 수집 | 반복별 2 steps | 반복별 1 step | 각 `SCENE_GC_OK` 표지 |
| Play→Stop 내용 복원 | PASS | PASS | `play-{debug,release}-gate.log` |
| W5 상태 머신·실패 주입·Undo 보존 | PASS | PASS | 동일 gate 로그와 구성별 Play 실행 산출물 |
| 리플렉션 컨테이너 형상 | PASS | PASS | `reflection-container.log` |
| 리플렉션 값·출력 형상 왕복 | 37 axes, failures 0 | 이 게이트는 Debug 전용 | `reflection-roundtrip.log` |

에디터 GC 소유 스레드 정적 계약 8개와 `verify-entity-ownership.ps1`도 통과했다.
정적 검사는 실제 PresentationThread의 모든 편집 동작에 대한 실행 증거로 확대하지 않는다.

Scene GC 반복의 각 실행은 다음을 확인한다.

- Scene/Entity/Component를 연결하는 두 component의 순환 참조가 최종 회수된다.
- 최종 cleanup 중 ScriptObjectHandle이 유효하고, cleanup 후 native/script handle이 무효화된다.
- 재사용 슬롯의 이전 generation handle은 되살아나지 않는다.
- 삭제와 DDOL 이송 시 단일·복수·simulation 선택 borrow가 제거된다.
- DDOL transfer root가 이송 중 객체를 보존하고 destination graph로 소유권을 넘긴다.
- 실제 component pin이 worker의 읽기와 join까지 메모리를 보존한다. 논리적 Destroy는 별도로 진행된다.
- 종료 시 live object 수와 quarantined 수가 실행 전 기준값으로 돌아오고 cycles_aborted는 증가하지 않는다.

Play 회귀는 재생 중 객체 추가 후 정지하여 편집 씬의 3개 객체와 내용 digest를 복원한다.
슬롯 순서는 재할당되어 달라질 수 있으나 내용은 복원됐다. W5는 snapshot 실패 시 Undo 이력 보존,
입력 소유권·커서·표시 타깃 전환과 확정 전 pause 거부를 검사했다.
Debug/Release 각각 두 Play 호스트 실행이 정상 종료됐다.

## 재현

```powershell
pwsh -NoProfile -File Tools/regression/verify-scene-gc.ps1 -Configuration Debug -Iterations 20
pwsh -NoProfile -File Tools/regression/verify-scene-gc.ps1 -Configuration Release -Iterations 20
```

Play 검사는 `Tools/regression/verify-play-roundtrip.ps1`에 구성별 `-Exe`와 절대 경로 `-Work`를 전달한다.
이번 실행의 원본 로그와 소스/바이너리 SHA-256은 `Artifacts/gc-validation-20261009`에 보관했다.
기존 dirty `active.workspace`와 `.rejected` 파일은 검증 전후 SHA-256이 동일했다.

## 관찰된 경고와 한계

- 첫 Debug Build는 reflgen overlay port 갱신 후 재실행을 요구하는 저장소 보호 장치로 중단됐다.
  새 도구로 다시 Build하여 통과했다.
- 첫 Debug Play 호출은 상대 Work 경로 때문에 입력 파일을 찾지 못했다. 절대 경로로 재실행하여 통과했다.
- Release 링크에 `Utility_Framework.pdb` 형식 레코드 손상 `LNK4020` 경고가 남는다.
  실행 검사는 통과했지만 일부 debugger symbol/type 접근은 제한될 수 있다.
- GC 호스트의 stderr에 `[profiler] shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]`가 남는다.
  이 경고의 profiler 수명 원인은 이번 GC fixture로 검증하지 않았다.
- 일부 문자 변환·매크로 재정의·지역 이름 가림·관리 코드 trimming 경고가 빌드 로그에 남는다.
- 250μs는 연성 예산이다. step 횟수는 지연시간/성능 수용이나 메모리 사용량 절감 증거가 아니다.
- packaged Player/Shipping, NativeAOT publish, ASan, 대규모 장시간 부하, prefab UI 반복,
  GC cleanup callback의 재진입/예외 주입, 종료 시 pending work 주입은 실행하지 않았다.

이번 검증은 검사한 GC 그래프와 Play 복원 범위에서 통과했다. 남은 제품 수용 범위는
`docs/design/GCCEIntegration.md`의 경계에 계속 남는다.
