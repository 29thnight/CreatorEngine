# PHASE 4.5 종료 검증 — 2026-10-11

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-temporal-foundation.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-temporal-foundation.ps1#L9) — `$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-temporal-foundation.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-temporal-foundation.ps1#L10) — `$out=Join-Path ([IO.Path]::GetFullPath($Work)) "native-$Config"`
- [Tools/regression/verify-temporal-foundation.ps1:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-temporal-foundation.ps1#L11) — `[IO.Directory]::CreateDirectory($out)|Out-Null`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

후속: 아래 당시 실패는 [DLSS 잔상 수정 검증](TemporalDlssDisocclusion20261011.md)에서 동일 입력으로 재현·수정하고 D/R warmed/startup 검증을 마쳤다. 이 문서의 표는 당시 실행 기록이며 전체 PHASE 완료 상태는 여전히 미완료다.

## 판정

**전체 종료 불가. 12/71 유지.** CPU 기록 비용 검증에서 벗어나 TU0/TU3의 실제 SDK 전환·실패 복구·품질 검사를 실행했다. 현재 정본은 `Artifacts/PR169/phase45-closure-20261011/checkpoint.json`이다. 이번 변경은 검증기와 회귀 검사이며 제품 렌더러 수정은 없다.

## 구현 및 실행

- 이전 전환 검사는 DLSS를 한 번도 선택하지 않았다. native/FSR/DLSS/XeSS 사이 모든 12개 방향을 통과하는 13개 상태열로 보완했다.
- 각 SDK의 capability/dispatch 실패 6개를 실제 성공 후 주입한다. FSR 실패는 native, DLSS/XeSS 실패는 실제 FSR 재구성으로 복구하며 마지막 native 상태를 확인한다.
- 현재 SDK 활성 Debug/Release 제품 바이너리에서 각각 13개 상태·6개 오류 복구가 통과했다. 요청 수신만으로 성공을 판정하지 않고 정확한 세대의 제출 프레임과 SDK 성공을 확인한다.
- `verify-temporal-foundation.ps1`가 실제 생산 코드를 연결해 Debug/Release에서 runtime/jitter/fallback, 개발 오류 제어, shipping 오류 주입 차단을 컴파일·실행한다. 두 구성 각 3개 실행 통과. 중복·충돌 capability 결과의 순서 독립 fail-closed 회귀를 추가했다.
- 품질 oracle 거부 후에도 GPU 검증과 정상 종료를 수집하도록 SDK 실행기를 수정했다. 거부는 계속 실패로 반환하며 성공 retention에 등록하지 않는다.

## 새로 확인한 종료 차단: DLSS Quality 첫 이동 프레임

1280×720 창, 실제 display 760×312/render 507×208의 고정 60 Hz SDK fixture이다. 모든 품질에서의 일반화나 물리 표시 속도 판정은 아니다.

| 실행 | 품질 | 전환/복구 | GPU 검증 | 종료 |
|---|---|---|---|---|
| Debug 직접 활성 1 | 실패 | 미실행 | 문제/메시지/누락 0 | 기존 실행기가 강제 종료 |
| Release 전환 후 | 통과 | 13상태/6복구 통과 | 모두 0 | 0 |
| Debug 전환 후 | 통과 | 13상태/6복구 통과 | 모두 0 | 0 |
| Debug 직접 활성 2 | 실패 | 미실행 | 모두 0 | 0 |

실패 두 건은 step 1의 HDR 평균 잔상 **0.278198 / p95 0.502693**, display **0.381797 / p95 0.608696**이다. 기존 기준 평균 0.10/p95 0.25를 변경하지 않았다. 두 실패의 raw 289개 해시가 모두 같았다. 두 번째 실패는 정상 종료하므로 강제 종료에 의한 측정 실패가 아니다.

기본 모션·깊이·입력 신원 검사는 통과한 뒤 품질 기준에서 거부됐다. 성공/실패 실행의 warmup 길이와 jitter 위상은 달랐다. 이는 조사 단서이며 원인이 확정됐다는 뜻은 아니다. 재시도 통과로 두 실패를 대체하지 않는다. DLSS SR의 전체 품질 수용은 미완료다.

## 종료까지 필요한 실제 게이트

1. **TU3**: 위 직접 활성 DLSS 실패 원인 수정 및 동일 기준 반복 수용. 이후 품질/해상도·startup/reset·실제 모델/가변 시간 검증.
2. **TR2/TR3/TU0/TU1**: 렌더/표시 분리, view/reset 수명, optional SDK 배포 및 독립 native 대조군의 전체 제품 수용. 이번 전환/CPU 계약 통과만으로 행 전체를 닫지 않는다.
3. **TU2/TU4**: 최신 바이너리 전체 품질·해상도·장면 검증. 과거 부분 검증을 현재 전체 수용으로 취급하지 않는다.
4. **TR0/FG0–FG4**: 남은 측정 UI와 실제 Player 생성 프레임 신원·리소스 최종 소비·HUD·pacing·복구 수용.
5. **TFG9**: NVIDIA/AMD/Intel/통합 GPU 지원 행렬 및 외부 물리 지연/표시 계측. 현재 RTX 2080 Ti만으로 DLSS FG 지원 장치나 다른 GPU의 실제 실행을 증명할 수 없다.

현재 실패 원인이 미확정이고 외부 장치 검증도 남아 있으므로 전체 종료 날짜를 확정할 근거는 없다. CPU 미세 최적화를 선행 조건으로 추가하지 않는다.

## 저장 관리

30 GiB 바닥을 유지했다. 해시가 동일한 첫 실패 raw는 두 번째 전체 실패를 보존한 뒤 제거했다. 성공은 구성별 frame-1 원본만 보존했다. 모든 판정·manifest·로그는 유지하며 삭제 ledger에 과거 전체 픽셀 재생 불가를 기록했다. 약 1.27 GiB 회수, 정리 후 실제 여유 **86.83 GiB**. 전체 바이너리 사본은 만들지 않았다.

</details>
