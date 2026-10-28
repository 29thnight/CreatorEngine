param([string]$Tag='20261026-module-accept1',[string]$ReportStem='ModulePerformanceAcceptance20261026')
$ErrorActionPreference='Stop';$root=$PSScriptRoot
$data=Get-Content "$root/results/$Tag-verdict.json" -Raw|ConvertFrom-Json
$destination=Join-Path $root "../../docs/analysis/$ReportStem.md"
$lines=@(
'# 모듈 바이너리 전체 성능 재수용 — 2026-10-26',
'',
"실제 측정 결과: **일반 컨테이너 $($data.generalAccepted)/82, 고정 MPSC 주 수용 batch8 $($data.mpscPrimaryAccepted)/4·개별 전송 추가 검사 $($data.mpscAccepted)/4, FAA 락 프리 큐 $($data.faaAccepted)/6, SPSC·기존 스핀 MPMC 처리량 $($data.concurrentAccepted)/8**. 전체 수용 판정은 **$(if($data.pass){'PASS'}else{'FAIL'})**다. 이 판정은 새 named module 바이너리와 이번 원시 표본에서 계산했다. 이전 헤더 바이너리의 통과 판정을 승계하지 않았다.",
'',
'## 고정한 수용 기준',
'',
'일반·확장 컨테이너와 고정 MPSC는 매 반복에서 custom/MSVC STL 또는 PPL 중앙값 비율 ≤1.10, 양쪽 각각 정렬 15표본 x[13]/x[1] ≤1.30이어야 한다. 일반 82개 조건은 각각 별도 프로세스에서 3회, 총 246개 프로세스로 실행했다. 고정 MPSC는 4개 조건을 3프로세스에서 실행했다. 모든 15표본을 보존했고 반복 2는 일반 조건 순서와 A/B 측정 순서를 반전했다.',
'',
'FAA 확장 MPMC는 기존 최적화 보고서의 6개 조건을 5프로세스에서 PPL과 비교했다. 8B/128B 값, 생산자·소비자 1/1·4/1·4/4이며 생산자당 65,536개를 전송한다. 전송 시간과 소멸 포함 시간 비율이 각 프로세스에서 모두 ≤1.10이어야 한다. FAA의 원래 기준에는 표본 편차 한도가 없으므로 이번에도 이를 추가하거나 완화하지 않았으며, 양쪽 편차는 표에 공개한다.',
'',
'## 환경과 구현 경계',
'',
'Windows x64 MSVC 19.50.35730, MSVC STL 145, Release, iterator debug 0, C++23이다. 실제 구현은 Creator.Containers 모듈의 22개 파티션에서 가져온다. 기존 정확성/비교 소스의 ce 구현 include를 제거하고 import를 삽입해 같은 작업을 실행했다. 측정용 STL/PPL 사용은 비교 실행 파일에만 있다. FAA 비교 소비자는 기존과 같은 /Ob3을 사용한다.',
'',
'일반 및 고정 MPSC는 이전 최종 수용과 같은 측정 스레드 CPU4/above-normal, 자체 SMT pause helper CPU5 조건이다. 자체 helper는 절대 시간을 늘리는 비교 환경 제어이며 라이브러리 최적화가 아니다. 다른 앱이나 OS 설정은 바꾸지 않았다. 일반 항목은 각 구현 100ms 목표로 독립 보정하고 각각 고정 1초 워밍업 후 15표본을 수집했다. 고정 MPSC는 OS yield 대기와 개별 전송 표본을 사용하며 생산자는 다른 물리 코어에 배치한다.',
'',
'FAA/PPL 5프로세스는 기존 queue_acceptance 프로토콜 그대로 SMT helper 종료 후 실행했다. 해당 프로토콜은 affinity 고정 없이 A/B 순서를 표본마다 번갈아 사용한다. 일반 단일 스레드/고정 MPSC와 다른 측정 조건임을 구분한다. 측정 중 소스와 바이너리를 수정하거나 빌드하지 않았다.',
'',
'## 일반·확장 82개 조건',
'',
'아래 비율 범위는 3프로세스의 중앙값 비율 범위이고, 편차 최대는 각 프로세스 안의 x[13]/x[1] 중 최대다. 프로세스 간 절대 시간 차이를 같은 통계로 해석하지 않는다.',
'',
'| 조건 | 비율 최소–최대 | 자체 편차 최대 | STL 편차 최대 | 3회 모두 수용 |',
'|---|---:|---:|---:|---|'
)
foreach($r in $data.general){$lines+='| {0} | {1:F4}–{2:F4} | {3:F4} | {4:F4} | {5} |' -f $r.key,$r.minRatio,$r.maxRatio,$r.maxCustomSpread,$r.maxReferenceSpread,$r.accepted}
$lines+=@('','### 배열 프로세스별 절대 시간 범위','','표본 내부가 안정적이어도 새 프로세스의 heap/주소·상태 등에 따라 절대 중앙값이 달라질 수 있다. 아래는 각 반복의 ns/작업 중앙값 범위이며 한도 1.30이 적용되는 통계와 다르다. 이 차이의 개별 원인은 이번 비교만으로 확정하지 않는다.','','| 배열 조건 | 자체 ns 최소–최대 | STL ns 최소–최대 | 자체/STL 프로세스 max/min |','|---|---:|---:|---:|')
foreach($r in $data.general|Where-Object {$_.key -like 'core/array_*'}){
 $a=$r.rows.customNs|Measure-Object -Minimum -Maximum;$b=$r.rows.referenceNs|Measure-Object -Minimum -Maximum
 $lines+='| {0} | {1:F0}–{2:F0} | {3:F0}–{4:F0} | {5:F3}/{6:F3} |' -f $r.key,$a.Minimum,$a.Maximum,$b.Minimum,$b.Maximum,($a.Maximum/$a.Minimum),($b.Maximum/$b.Minimum)
}
$lines+=@('','## 고정 MPSC 대 PPL','','기존 주 수용 조건 yield-batch8도 같은 모듈 바이너리로 3회 실행했다. 이전 주 수용 프로토콜과 같이 SMT helper 없이 CPU4 측정 스레드와 분리한 생산자 코어를 사용한다. 3개 고정 워밍업 묶음, 정식 15개 묶음 각각 하위 전송 8개이며 그 평균 시간을 쓴다. 연속 단일 wall interval이나 단일 전송 지연 안정성을 뜻하지 않는다. 하위 전송 1,440쌍 전부를 보존하고 묶음 평균을 재계산했다. 진단용 pause 모드로 바꾸지 않았다.','','| 주 수용 조건/용량/생산자 | 비율 최소–최대 | 자체 편차 최대 | PPL 편차 최대 | 3회 모두 수용 |','|---|---:|---:|---:|---|')
foreach($r in $data.mpscPrimary){$lines+='| {0} | {1:F4}–{2:F4} | {3:F4} | {4:F4} | {5} |' -f $r.key,$r.minRatio,$r.maxRatio,$r.maxCustomSpread,$r.maxReferenceSpread,$r.accepted}
$lines+=@('','아래는 별도 SMT 제어 조건에서의 개별 전송 추가 검사다. 주 수용 batch8과 결과를 혼합하지 않는다.','','| 개별 용량/생산자 | 비율 최소–최대 | 자체 편차 최대 | PPL 편차 최대 | 3회 모두 수용 |','|---|---:|---:|---:|---|')
foreach($r in $data.mpsc){$lines+='| {0} | {1:F4}–{2:F4} | {3:F4} | {4:F4} | {5} |' -f $r.key,$r.minRatio,$r.maxRatio,$r.maxCustomSpread,$r.maxReferenceSpread,$r.accepted}
$lines+=@('','## FAA 확장 MPMC 대 PPL','','| 값 B/생산자/소비자 | 전송 비율 최소–최대 | 소멸 포함 비율 최대 | 자체/PPL 편차 최대 | 5회 모두 수용 |','|---|---:|---:|---:|---|')
foreach($r in $data.faa){$life=($r.rows.lifecycleRatio|Measure-Object -Maximum).Maximum;$lines+='| {0} | {1:F4}–{2:F4} | {3:F4} | {4:F4}/{5:F4} | {6} |' -f $r.key,$r.minRatio,$r.maxRatio,$life,$r.maxCustomSpread,$r.maxReferenceSpread,$r.accepted}
$lines+=@('','## SPSC 및 기존 스핀 MPMC 추가 비교','','과거 concurrent 프로토콜도 빠짐없이 실행했다. 생산자당 16,000개, 2회 워밍업·9표본·3프로세스이며 아래 판정은 각 반복 처리량 비율 ≤1.10이다. 기존 보고서와 같이 x[7]/x[1]>1.25는 별도 편차 경고다. 스핀 MPMC는 락 프리 타입이 아니며 SPSC도 역할 고정 큐이므로 PPL 전체의 대체 가능성을 뜻하지 않는다.','','| 타입/값 B/생산자/소비자 | 비율 최소–최대 | 자체/PPL 편차 최대 | 처리량 3회 모두 수용 |','|---|---:|---:|---|')
foreach($r in $data.concurrent){$lines+='| {0} | {1:F4}–{2:F4} | {3:F4}/{4:F4} | {5} |' -f $r.key,$r.minRatio,$r.maxRatio,$r.maxCustomSpread,$r.maxReferenceSpread,$r.accepted}
$failures=@($data.general+$data.mpsc+$data.faa+$data.concurrent+$data.mpscPrimary|Where-Object {!$_.accepted})
$lines+=@('','## 미수용 항목과 판단','')
if(!$failures.Count){$lines+='이번 범위의 모든 조건이 기존 기준을 충족했다.'}else{
 $lines+='아래 조건은 이번 바이너리에서 미수용이다. 실패 표본을 제거하거나 통과 재실행으로 교체하지 않았다.'
 $lines+=''
 foreach($r in $failures){$lines+='- {0}: 최대 비율 {1:F4}, 자체/STL·PPL 편차 최대 {2:F4}/{3:F4}.' -f $r.key,$r.maxRatio,$r.maxCustomSpread,$r.maxReferenceSpread}
 $lines+=''
 $lines+='편차가 기준 안인데 시간 비율이 초과한 항목은 이번 통계상 안정적인 성능 미달이다. 편차 한도를 초과한 항목은 안정성 기준 미달로 함께 기록한다. 코드 생성과 인라이닝 차이는 후속 대조 후보이며, 이번 전체 수집만으로 개별 원인을 확정하지 않는다.'
 $lines+=''
 $lines+='| 미수용 조건 | 실패 반복 | 반복별 전송/작업 비율 |'
 $lines+='|---|---|---|'
 foreach($r in $failures){
  $failed=@($r.rows|Where-Object {!$_.accepted}|ForEach-Object {$_.run}) -join ', '
  $ratios=@($r.rows|ForEach-Object {'{0}: {1:F4}' -f $_.run,$_.ratio}) -join '; '
  $lines+='| {0} | {1} | {2} |' -f $r.key,$failed,$ratios
 }
 $lines+=''
 $lines+='미수용은 전체 반복의 통과를 요구한 결과다. 일부 반복만 미달한 조건도 통과로 바꾸지 않는다. FAA의 소멸 포함 최대 비율은 위 FAA 표를 참조한다. SPSC·스핀 MPMC는 처리량 통과와 표본 편차 경고를 구분한다.'
}
$lines+=@('','## 증거와 재현','',
"독립 라이브러리 results/$Tag-verdict.json이 판정 정본이며, $Tag-before.json과 $Tag-environment.json에 소스·스크립트·바이너리 SHA256을 보존한다. guard JSON과 246개 case 파일의 시간 범위를 대조하고 원시 표본에서 요약 비율과 편차를 다시 계산했다. 전체 표본 파일의 해시도 판정 JSON에 기록한다.",
'',
'총 324개 조건/프로세스 결과다. 일반 246·고정 MPSC 개별 12·FAA 30개 결과는 각각 15표본, SPSC·스핀 MPMC 24개 결과는 기존 방식의 9표본으로 총 4,536쌍이다. 주 수용 MPSC 12개 결과의 180개 묶음 표본(하위 전송 1,440쌍)도 추가 보존했다. 워밍업은 이 수에 포함하지 않는다. 컴파일러 및 실제 비교한 STL/PPL 설치 파일 해시도 reference-files JSON에 남겼다.',
'',
'재현은 새 Tag와 별도 GuardTag로 experiments/20261026-small-array/with-guard.ps1 안에서 run-module-acceptance.ps1을 실행한다. guard 태그는 측정 태그와 다른 접두사를 사용한다. 이어 module_queue_acceptance.exe를 guard 없이 5회 실행해 <Tag>-ppl-1.csv…5.csv, module_concurrent_tests.exe를 3회 실행해 <Tag>-concurrent-1.csv…3.csv로 보존한다. 별도 run-module-mpsc-primary.ps1 -Tag <Tag>로 주 수용 batch8을 수집하고 verify-module-acceptance.ps1 -Tag <Tag> -GuardTag <GuardTag>로 검증한다. 수집 전에 reference-files JSON과 module-concurrent-before JSON에 비교 설치 파일 및 concurrent 실행 파일 해시를 기록한다. 빌드는 수집 전에 modules/validate-msvc.ps1을 실행한 뒤 module_library_benchmark·module_mpsc_benchmark·module_queue_acceptance 타깃을 Release로 빌드한다.',
'',
'모듈 정확성·ASan·Linux 실행 검증은 [모듈 구현 보고서](ContainerModules20261026.md), 이전 헤더 수용은 [이전 수용 보고서](SmallArrayVarianceAcceptance20261026.md)를 참조한다. 다른 OS의 성능 동등성이나 실제 엔진 프레임 수용을 입증한 결과는 아니다. 엔진 통합과 PPL 별칭 변경은 수행하지 않았다.'
)
[IO.File]::WriteAllText($destination,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
"Wrote $destination"
