$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$engine=Join-Path (Split-Path $root) 'CreatorEngine'
$work=Join-Path $engine 'Build/Verification/ContainerMigration/VarianceReacceptance20261027'
$tag='20261027-variance-window-full1'
$verdictPath="$root/results/$tag-verdict.json"
$v=Get-Content $verdictPath -Raw|ConvertFrom-Json
$values=@(Get-Content "$root/results/20261027-variance-window-values-summary.json" -Raw|ConvertFrom-Json)
$preserved=Get-Content "$work/preserved-state.json" -Raw|ConvertFrom-Json
if(!$v.pass -or !$v.verified -or !$preserved.pass -or $values.Count -ne 21 -or @($values|Where-Object {!$_.pass}).Count){throw 'Acceptance incomplete'}
foreach($raw in $v.rawFiles){if((Get-FileHash $raw.Path).Hash -ne $raw.Hash){throw "Raw hash mismatch: $($raw.Path)"}}
$windows=@(Get-Content "$root/results/$tag-observer-windows.json" -Raw|ConvertFrom-Json)
$intervals=@(Get-Content "$root/results/$tag-process-intervals.jsonl"|ForEach-Object {$_|ConvertFrom-Json})
$overlaps=@(foreach($process in $intervals){foreach($window in $windows){if(([DateTimeOffset]$process.started -lt [DateTimeOffset]$window.measuringAllowedAt) -and ([DateTimeOffset]$process.ended -gt [DateTimeOffset]$window.paused)){[pscustomobject]@{process=$process;window=$window.stage}}}})
if($intervals.Count -ne 246 -or $overlaps.Count){throw 'Observer window overlap'}
$guard=Get-Content "$root/results/20261027-variance-window-guard1-guard.json" -Raw|ConvertFrom-Json
if(!$guard.success -or !$guard.aliveAtWorkEnd){throw 'Guard invalid'}
$audit=[ordered]@{pass=$true;generalProcesses=$intervals.Count;observerWindowOverlaps=$overlaps.Count;rawFilesRehashed=$v.rawFiles.Count;valueRows=$values.Count;valuePass=$true;canonicalFilesUnchanged=66;priorSourceEntriesUnchanged=193;conditions='High priority class for own general/value child and CPU5 pause helper; general CPU4; analysis only between complete processes; fixed 3s settle; unchanged original gates and binaries'}
$audit|ConvertTo-Json -Depth 4|Set-Content "$work/final-audit.json" -Encoding utf8
Copy-Item $verdictPath "$work/full-verdict.json"
Copy-Item "$root/results/20261027-variance-window-values-summary.json" "$work/values-summary.json"
Copy-Item "$root/results/$tag-observer-windows.json" "$work/observer-windows.json"
$metaPath="$engine/Engine/Containers/integration-source.json"
$m=Get-Content $metaPath -Raw|ConvertFrom-Json
$m.fullModuleReacceptanceHistory=@($m.fullModuleReacceptanceHistory)+@($m.fullModuleReacceptance)
$m.fullModuleReacceptance=[pscustomobject]@{tag=$tag;pass=$true;verdictSHA256=(Get-FileHash $verdictPath).Hash;generalAccepted=82;generalTotal=82;mpscSingleAccepted=4;mpscPrimaryAccepted=4;faaAccepted=6;concurrentAccepted=8;policy=$v.policy;evidence='Build/Verification/ContainerMigration/VarianceReacceptance20261027/full-verdict.json';measurementConditions=$audit.conditions}
$m.currentPerformanceAcceptance='PASS: 82/82 general, 4/4 MPSC single, 4/4 MPSC primary, 6/6 FAA-PPL, 8/8 concurrent, 7/7 values (three processes). Existing thresholds unchanged. Controlled Windows CPU4/5 High-priority observer-separated measurement; not an all-OS or frame-performance claim.'
$m.valueTypes.performanceEvidence='Build/Verification/ContainerMigration/VarianceReacceptance20261027/values-summary.json'
$m|Add-Member -Force NoteProperty latestPerformanceReport 'docs/analysis/ContainerVarianceReacceptance20261027.html'
$m|ConvertTo-Json -Depth 40|Set-Content $metaPath -Encoding utf8
$targets=@($v.general|Where-Object key -in @('core/map_hit/65536','core/map_miss/65536','core/string_find/65536'))
$table=($targets|ForEach-Object {"| $($_.key) | $($_.maxRatio) | $($_.maxCustomSpread) | $($_.maxReferenceSpread) | PASS |"}) -join "`n"
$doc=@'
# 컨테이너 편차 원인 분리와 전체 성능 재수용 — 2026-10-27

엔진 내부 정본을 수정하지 않고 전체 재수용에 통과했다. 기존 3건은 중앙값 성능이 아니라 15개 표본의 편차 기준 미달이었다. 새 캠페인은 원시 결과 전체를 보존하고 기존 기준을 그대로 적용했다.

## 최종 결과

| 검증군 | 통과 | 반복 |
|---|---:|---:|
| 일반 모듈 컨테이너 | 82/82 | 각 3개 프로세스 |
| MPSC 단일 전송 | 4/4 | 3회 |
| MPSC 주 기준 | 4/4 | 3회 |
| FAA / PPL 확장 큐 | 6/6 | 기존 5회 게이트 |
| 동시성 | 8/8 | 3회 |
| pair / tuple / get / tie / apply | 7/7 | 3개 프로세스 |

일반·MPSC 기준은 각 반복 CE/STL 또는 CE/PPL 중앙값 비율 ≤1.10, 양측 정렬 15표본의 x[13]/x[1] ≤1.30이다. 두 번째 반복의 역순 실행, 100ms 목표 시간, 1초 워밍업을 유지했다. FAA는 기존 5프로세스 전송·수명 비율 게이트를 유지하며 편차를 별도 공개한다. 실패 표본 삭제, 합격 표본 짜깁기, 기준 완화는 하지 않았다.

### 앞서 실패한 3건

아래는 3회 중 최댓값이다. 비율은 작을수록 빠르다.

| 항목 | CE/STL 비율 | CE 편차 | STL 편차 | 판정 |
|---|---:|---:|---:|---|
__TABLE__

## 원인 분리

이전 `20261027-values-format-full1`은 79/82였다. map_hit/65536의 STL 편차 2.6496, map_miss/65536의 STL 1.4708, string_find/65536의 CE 1.3534가 실패 원인이다. 해당 실패 기록은 유지한다.

별도 계측용 헤더 바이너리에서 세 항목 × 다섯 부하 조건 × 두 우선순위, 총 30개 프로세스를 실행했다. 실제 수용에는 기존 모듈 바이너리를 사용했다. 같은 CPU4의 경쟁 작업을 고정했을 때 다음과 같이 재현됐다.

| 항목 | Normal CE/STL 편차 | High CE/STL 편차 | Normal 벽시계−CPU시간 상위값 CE/STL(ms) | High(ms) |
|---|---|---|---|---|
| map_hit | 1.02 / 2.93 | 1.04 / 1.11 | 6 / 320 | 7 / 7 |
| map_miss | 2.81 / 2.65 | 1.03 / 1.08 | 220 / 311 | 10 / 11 |
| string_find | 3.20 / 3.55 | 1.02 / 1.03 | 284 / 283 | 2 / 10 |

상위값은 15표본 정렬 x[13]이다. 이 실험은 스케줄링 선점으로 큰 편차가 발생하는 경로를 분리했다. 원래 실패 시점의 특정 외부 프로그램이나 유일한 원인을 확인한 것은 아니다. 해당 30개 프로세스에서 페이지 폴트 증가량은 0이었다. PDH DPC·인터럽트 피크는 0이 아니므로 인터럽트를 배제하지 않는다. QueryThreadCycleTime을 하드웨어 PMU 주파수나 캐시 미스로 해석하지 않았다.

High 우선순위만 적용한 첫 전체 캠페인에서도 map_miss STL 편차 1.4993이 발생해 즉시 실패로 보존했다. 그 시점의 대량 관측 출력이 간섭했을 가능성은 있으나 인과관계가 입증되지는 않았다. CPU4/8 및 별도 코어의 64MB 순차 메모리 부하를 추가한 12개 프로세스에서는 기준 상태 편차 약 1.02~1.09, 스트림 부하 최대 약 1.1372였으며 1.50을 재현하지 못했다. 메모리 대역폭이나 특정 코어만을 단독 원인으로 단정할 근거가 없다. ETW/하드웨어 PMU 계측은 수행하지 않았다.

## 측정 환경 통제

최종 `20261027-variance-window-full1`에서는 기존 바이너리·입력·표준 라이브러리 참조 파일을 해시 검증하고 재빌드하지 않았다. 일반 측정 CPU4, 기존 CPU5 pause 보조 작업을 유지하고, 자체 일반·값 타입 측정 프로세스와 보조 작업만 High 우선순위로 시작했다. 다른 앱의 우선순위·친화도나 OS 설정은 변경하지 않았다. 인위적인 경쟁 부하는 최종 수용에 넣지 않았다.

일반·값 타입은 전체 프로세스가 끝난 뒤에만 분석·상태 출력을 허용하고 재개 후 고정 3초를 기다렸다. 일반 246개 프로세스의 시작·종료 시간과 관측 허용 구간을 대조한 중첩은 0이다. MPSC 주 기준의 긴 프로세스 중에는 짧은 상태 안내가 있었으므로 이 분리 보장을 모든 큐 계측으로 확대하지 않는다. CPU5 보조 작업은 작업 종료 시까지 살아 있었다.

일반 모듈은 기존 MSVC 195035730 / STL 145 바이너리, 값 타입은 기존 MSVC 195136260 / STL 145 바이너리를 사용했다. 결과는 현재 Windows / i7-8700K 환경에서 선언한 통제 조건의 수용 결과다. 다른 OS의 일정한 성능이나 실제 프레임 성능 향상을 증명하지 않는다.

## 문자열·소유권·수명·할당 보존

컨테이너 정본 66개와 기존 전환 소스 193개, 기존 엔진 바이너리 2개가 이전 해시와 일치했다. 문자열·컨테이너 알고리즘·메모리 순서·락 프리 구현은 수정하지 않았다. 정본 파일 해시 목록은 그대로 두고 수용 메타데이터만 갱신했다. 이전 실패와 더 오래된 합격 이력도 유지한다.

기존 네이티브 값 타입 실행 파일을 다시 실행한 결과 Debug 4035, Release 4034, ASAN 4034 검사가 통과했다. Debug의 8192회 값 타입 연산은 allocations=0이며 소유권·참조 수명 계약 검사도 통과했다. Release·ASAN은 allocationChecks=0이므로 이 둘을 무할당 계측 증거로 사용하지 않는다. 기존 모듈 기능 검사도 다시 통과했다.

이번 작업에서는 엔진을 다시 빌드하거나 Editor/DX12 실행 검증을 새로 수행하지 않았다. 이전 실제 엔진 실행 증거는 동일 소스·바이너리 해시로 연결한다. 추가 STL 사용처 전환은 수행하지 않았다.

## 증거

- [전체 판정](../../Build/Verification/ContainerMigration/VarianceReacceptance20261027/full-verdict.json), [값 타입 21개 결과](../../Build/Verification/ContainerMigration/VarianceReacceptance20261027/values-summary.json)
- [소스 보존·기능 검사](../../Build/Verification/ContainerMigration/VarianceReacceptance20261027/preserved-state.json), [최종 독립 감사](../../Build/Verification/ContainerMigration/VarianceReacceptance20261027/final-audit.json)
- [관측 허용 시간 기록](../../Build/Verification/ContainerMigration/VarianceReacceptance20261027/observer-windows.json)
- 외부 원시 증거: `Tests/Containers/results/20261027-variance-window-full1-*`, `20261027-variance-window-values-*`, `20261027-variance3-*`, `20261027-execution-*`.
- 실패 캠페인: `20261027-values-format-full1`, `20261027-variance-high-full1`; 초기 실험 실패 로그도 유지했다.
- 재현 드라이버: `Tests/Containers/experiments/20261027-variance-isolation/run-window-full-suite.ps1`. 새 태그와 별도의 값 타입 출력 경로로 실행해야 기존 증거를 덮어쓰지 않는다.
'@
$doc=$doc.Replace('__TABLE__',$table)
$doc|Set-Content "$engine/docs/analysis/ContainerVarianceReacceptance20261027.md" -Encoding utf8
$audit|ConvertTo-Json -Depth 4
