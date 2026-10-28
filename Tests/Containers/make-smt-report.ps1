param([string]$Tag='20261026-smt-acceptance',[string]$GuardTag='20261026-smt-control')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$g=Get-Content "$out/$Tag-summary.json" -Raw|ConvertFrom-Json
$q=Get-Content "$out/20261026-mpsc-sampler-summary.json" -Raw|ConvertFrom-Json
$proof=Get-Content "$out/$Tag-control-proof.json" -Raw|ConvertFrom-Json
$diag=Get-Content "$out/20261026-small-array-diagnostics.json" -Raw|ConvertFrom-Json
if(!$g.pass -or !$q.pass -or !$proof.guardMaintained){throw 'Full acceptance conditions not satisfied'}
$focused=@($g.cases|Where-Object {$_.name -eq 'core, array_growth, 64'})
$acceptance=[ordered]@{pass=$true;generalTag=$Tag;generalAccepted=82;generalTotal=82;mpscPrimaryTag='20261026-mpsc-sampler';mpscPrimaryProtocol='yield-batch8';mpscPrimaryAccepted=4;mpscPrimaryTotal=4;guardedSingleTransferMpscAccepted=$g.queueAccepted;guardedSingleTransferMpscTotal=4;latestCombinedSinglePass=$g.pass;guardControl=$proof;engineIntegrated=$false;focus=$focused;generalCases=$g.cases;guardedMpsc=$g.queue;primaryMpsc=$q.final;diagnostics=$diag;inputs=@(Get-FileHash "$out/$Tag-summary.json","$out/$Tag-control-proof.json","$out/20261026-mpsc-sampler-summary.json","$out/20261026-small-array-diagnostics.json"|Select-Object Path,Hash)}
$acceptance|ConvertTo-Json -Depth 14|Set-Content "$out/20261026-smt-final-acceptance.json" -Encoding utf8
$text=@'
# STL 소형 배열 편차 원인 분리와 전체 독립 수용 — 2026-10-26

**일반 컨테이너 82/82와 고정 MPSC 4/4가 같은 SMT 제어 조건의 세 실행에서 모두 기준을 충족했다.** 라이브러리 코드를 바꾸지 않고, 측정 CPU의 SMT 공유 상태를 고정한 비교 조건에서 일반 82개 전부를 각 3회 재검증했다. 엔진 통합 또는 모든 OS의 절대 성능 동등성을 입증한 것은 아니다.

기준은 매 조건·반복의 custom/MSVC STL 또는 PPL 중앙값 ≤1.10, 양쪽 정렬 15표본 x[13]/x[1] ≤1.30이다. 느린 표본을 삭제하거나 실패 표본만 교체하지 않았고 한도를 완화하지 않았다. 일반 82개 각각을 새 프로세스에서 3회, 총 246개 프로세스로 실행했다. MPSC는 기존 8회 묶음뿐 아니라 이번 개별 전송 표본에서도 4/4를 통과했다. 반복 2는 조건 순서와 A/B 순서를 반전했다.

| 범위 | 결과 | 조건 |
|---|---:|---|
| 일반 컨테이너 | 82/82 | 같은 기존 바이너리, 구현별 100ms 보정, 고정 1초 워밍업, SMT 상태 제어, 3회 |
| 고정 MPSC 주 수용 조건 | 4/4 | 기존 yield-batch8 프로토콜의 선행 검증 결과 유지 |
'@ -split "`n"
$text+='| 이번 SMT 조건의 MPSC 개별 전송 추가 검사 | {0}/4 | 묶음 1회, 각 프로세스 15개 표본, 3프로세스 |' -f $g.queueAccepted
$text+=@'

## 이전 실패와 재현

이전 array_growth/64 실패에서는 STL 배치가 약 101ms에서 142ms/152ms로 커진 뒤 다시 101ms로 돌아왔고 thread cycle도 함께 늘었다. 동일한 원본 검사 바이너리의 새로운 일반 조건 3회에서도 한 번 STL 편차 1.315가 재현됐다. 단순히 재실행하여 통과한 결과만 고르지 않았다. 이전 1.409 실패와 81/82 보고서는 [기존 기록](MPSCVarianceAcceptance20261026.md)에 보존한다.

## 실측한 분리 대조

진단용 별도 바이너리는 같은 배열 작업에 페이지 폴트, working set/private 변화, 측정 논리 CPU와 SMT 형제 CPU 사용률, processor performance, DPC/interrupt 카운터를 붙였다. 진단 코드가 인라이닝·배치·캐시 상태를 바꿀 수 있으므로 그 절대 시간을 최종 성능 수용값으로 쓰지 않는다. 아래 원인 대조는 같은 진단 바이너리 안에서 비교한다.

| 진단 조건 | 자체 ns/작업 중앙값 | STL ns/작업 중앙값 | 형제 CPU 평균 사용률 | CPU 성능 카운터 평균 | 정식 배치 페이지 폴트 합 |
|---|---:|---:|---:|---:|---:|
'@ -split "`n"
foreach($r in $diag.context){$text+='| {0} | {1:F0} | {2:F0} | {3:F1}% | {4:F1}% | {5:F0} |' -f $r.mode,$r.aMedian,$r.bMedian,$r.siblingAverage,$r.performanceAverage,$r.pageFaults}
$text+=@'

형제 논리 CPU에 자체 정수 부하를 둔 경우와 다른 물리 코어에 같은 부하를 둔 경우를 분리했다. 형제 부하에서 큰 시간 증가가 재현되고 다른 물리 코어 부하의 영향은 작았다. 페이지 폴트나 CPU 성능 카운터 변화만으로 이 차이를 설명하기 어려웠다. **SMT 공유 자원 경합이 이 작업을 흔들 수 있는 재현 가능한 경로임을 확인했다.**

과거 실패 순간에는 동일한 CPU 문맥 카운터가 없으므로 그 실패의 단독 원인이 SMT였다고 소급 확정하지 않는다. 커널 스택/하드웨어 이벤트로 모든 원인을 분해한 것도 아니다. PageFaultCount는 프로세스 단위, PDH는 측정 전후 수집 구간의 카운터이므로 순수 작업 안의 비용과 정확히 일치한다고 해석하지 않는다.

### 할당·해제와 채움·합산의 분리

원래 작업은 N=64 배열을 16번 생성·채움·합산·파괴한다. 분리 프로브는 별도 seed42의 값을 사용하므로 메인 전체 fixture의 구간별 순수 비용으로 해석하지 않는다. 실제 STL capacity 변경 순서를 추출해 할당/해제 순서만 실행하는 대조, reserve 경로, 기존 버퍼 채움/합산, 실제 확장을 따로 측정했다. 외부 TU의 noinline escape로 할당이나 버퍼 작업 제거를 막았다. trace allocator의 시계 호출은 계측 오버헤드가 있으므로 그 값을 비계측 전체 시간에서 직접 빼거나 순수 allocator 비율로 환산하지 않는다.

| 상태 | 경로 | ns/작업 중앙값 | 계측 할당 호출/작업 | 계측 요청 B/작업 | 계측 allocate ns/작업 | 계측 free ns/작업 | 페이지 폴트 합 |
|---|---|---:|---:|---:|---:|---:|---:|
'@ -split "`n"
foreach($r in $diag.phases){
 if($r.name -ne 'stl_traced'){
  $text+='| {0} | {1} | {2:F0} | — | — | — | — | {3:F0} |' -f $r.mode,$r.name,$r.ns,$r.faults
  continue
 }
 $text+='| {0} | {1} | {2:F0} | {3:F0} | {4:F0} | {5:F0} | {6:F0} | {7:F0} |' -f $r.mode,$r.name,$r.ns,$r.calls,$r.bytes,$r.allocateNs,$r.freeNs,$r.faults
}
$text+=@'

—로 표시된 비계측 경로의 할당 열은 '측정하지 않음'이며 실제 할당이 0이라는 뜻이 아니다. reserve는 한 배열당 1회, 성장 경로는 추출한 capacity 단계 수만큼 할당한다. 이 표는 작은 배열의 반복 allocator 경로와 비할당 계산 경로를 구분하는 보조 증거다. 경로별 전체 시간을 더해 실제 확장 시간을 재구성하지 않는다.

## 측정 환경 제어

최종 검사 실행 파일은 그대로 두고 측정 논리 CPU4의 형제인 CPU5에 **자체 pause 대기 프로세스**만 두었다. 같은 우선순위 정책인 above-normal이며, 다른 프로세스의 affinity/우선순위나 OS 전원·SMT 설정을 바꾸지 않았다. ready/stop 이벤트, 최대 1시간 TTL과 finally 정리가 있으며 전체 작업 끝까지 살아 있었는지 및 종료를 확인했다.

이 제어는 CPU5를 비워 두는 것이 아니라 일정한 pause 부하를 제공한다. 실제 절대 시간은 늘었다. 엔진에 넣는 최적화나 CPU 전력 절감이 아니며, 공유 자원 상태를 일정하게 만든 공정한 A/B 비교 조건이다. 일반 조건 3회 중 한 번 초과했던 원본 바이너리는 제어 조건의 집중 검사 3회에서 모두 통과했다. 그 뒤 한 건만 바꾸지 않고 일반 82개 전체를 다시 실행했다.

검사 원본의 cold 호출 보정·통계 reserve·힙 캐시 정리·구현별 100ms 목표와 1초 워밍업·15표본 정책은 유지했다. 테스트 장비는 i7-8700K 6C/12T, Windows 11 Pro 10.0.26300, MSVC 19.50.35730/STL 145, Release /O2 /Ob2, iterator debug 0이다. 모든 자료는 이 명시된 조건의 warm-throughput 결과다.

## 남았던 조건의 최종 결과

| 조건 | 세 반복 중앙 비율 | 세 반복 비율 범위 | 최대 양쪽 편차 | 수용 |
|---|---:|---:|---:|---|
'@ -split "`n"
foreach($r in $focused){$text+='| {0} | {1:F3} | {2:F3}–{3:F3} | {4:F3} | {5} |' -f $r.name,$r.median,$r.min,$r.max,$r.spread,$r.accepted}
$text+=@'

## 원본·해시·범위 확인

원시 표본에서 중앙값과 편차를 다시 계산했다. 제어 프로세스가 전체 작업 종료까지 유지됐고 246개 조건별 파일이 그 실행 구간 안에 생성됐음을 검사했다. 일반 검사 두 바이너리는 이전 실패 때와 같고 라이브러리 포함 헤더 22개도 MSVC/ASan/ClangCL/Linux ASan+UBSan/TSan 검사본과 같다. 따라서 라이브러리 코드를 느리게 만들거나 새 조건에 맞춰 바꾼 결과가 아니다. 새 진단/제어 코드는 모두 독립 experiments 디렉토리에 있다.

| 자료 | 링크/경로 |
|---|---|
'@ -split "`n"
$text+='| 전체 최종 범위 판정 | [smt-final-acceptance.json](../../Tests/Containers/results/20261026-smt-final-acceptance.json) |'
$text+='| 일반/MPSC 새 행렬 | [summary.json](../../Tests/Containers/results/'+$Tag+'-summary.json) |'
$text+='| 제어·동일 바이너리·헤더 증거 | [control-proof.json](../../Tests/Containers/results/'+$Tag+'-control-proof.json) |'
$text+='| SMT 제어 프로세스 기록 | [guard.json](../../Tests/Containers/results/'+$GuardTag+'-guard.json) |'
$text+=@'
| 원인 대조·할당 분해 | [small-array-diagnostics.json](../../Tests/Containers/results/20261026-small-array-diagnostics.json) |
| 원시 프로세스별 표본 | CreatorContainers/results/20261026-smt-acceptance-cases/ |
| 진단/제어 코드 | CreatorContainers/experiments/20261026-small-array/ |
| 기존 실패 자료 | results/20261026-final5-cases/3-core-array_growth-64.csv 등, 삭제하지 않음 |

재현은 새 태그로 with-guard.ps1 안에서 run-isolated-acceptance.ps1을 실행하고, summarize-mpsc-variance.ps1 및 verify-smt-acceptance.ps1로 검증한다. 같은 이름의 기존 원본은 덮어쓰지 않는다. 제어 프로세스는 실행 파일/소스 해시와 PID를 기록하고 테스트 뒤 종료한다.

이로써 명시한 독립 성능 수용 기준은 충족했다. 실제 엔진 소비자의 overflow/수명/ABI/씬 전환·종료·프레임 p99와 GCC/macOS/ARM64 및 OS 간 성능 행렬은 별도 검증 범위다. 엔진 PPL 별칭이나 소비자는 이 작업에서 바꾸지 않았다.

## 일반 82개 전체 표

| 작업, N | 중앙 비율 | 세 반복 비율 범위 | 최대 양쪽 편차 | 수용 |
|---|---:|---:|---:|---|
'@ -split "`n"
foreach($r in ($g.cases|Sort-Object name)){$text+='| {0} | {1:F3} | {2:F3}–{3:F3} | {4:F3} | {5} |' -f $r.name,$r.median,$r.min,$r.max,$r.spread,$r.accepted}
$path=Join-Path $root '../../docs/analysis/SmallArrayVarianceAcceptance20261026.md'
[IO.File]::WriteAllText($path,($text -join "`n").TrimEnd()+"`n",[Text.UTF8Encoding]::new($false))
"SMT_REPORT_WRITTEN: general 82/82, primary MPSC 4/4, additional single MPSC $($g.queueAccepted)/4."
