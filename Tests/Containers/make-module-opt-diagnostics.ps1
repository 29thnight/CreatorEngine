param([string]$AcceptanceTag='20261026-module-opt-accept1')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$d=Get-Content "$out/20261026-modopt-paired-analysis.json" -Raw|ConvertFrom-Json
$gate=Get-Content "$out/$AcceptanceTag-verdict.json" -Raw|ConvertFrom-Json
function Med($v){$a=@($v|Sort-Object);$a[[int][math]::Floor($a.Count/2)]}
function Range($v){$m=$v|Measure-Object -Minimum -Maximum;'{0:F4}–{1:F4}' -f $m.Minimum,$m.Maximum}
$lines=@('# 모듈 성능 미달 원인 분리와 최적화 — 2026-10-26','',
"최종 전체 모듈 수용 판정은 **$(if($gate.pass){'PASS'}else{'FAIL'})**다. 일반 $($gate.generalAccepted)/82, MPSC 주 수용 $($gate.mpscPrimaryAccepted)/4·개별 추가 검사 $($gate.mpscAccepted)/4, FAA $($gate.faaAccepted)/6, SPSC·스핀 MPMC 처리량 $($gate.concurrentAccepted)/8이다. 전 조건의 반복별 결과는 [전체 재수용 표](ModulePerformanceOptimization20261026.md)를 참조한다.",
'',
'[이전 미수용 결과](ModulePerformanceAcceptance20261026.md)는 원시 표본과 함께 보존했다. 실패 항목만 재실행해 교체하지 않고, 최적화 후 82개 일반 조건 각각 3회·고정 MPSC 개별/8회 묶음 각각 3회·FAA 6조건 5회·SPSC/스핀 MPMC 8조건 3회를 새 바이너리로 전부 수집했다.',
'',
'## 분리한 비용과 변경',
'',
'진단용 작은 연산 검사에서 32비트 원자 load/store 1,024회는 2,690ns, 표준 원자 대조는 782ns였다. 64비트 fetch_add/load는 8,020/8,010ns로 거의 같았다. 문자 리터럴 검색 인자 생성 1,024회는 4,982/476ns였다. 진단의 원자 32비트 대조는 acquire load/release store이며 64비트는 기존 sequentially consistent 동작을 유지한다. 리터럴 생성 대조는 상수 길이를 최적화할 수 있게 같은 장벽을 사용했으므로 범용 문자열 생성 비용을 뜻하지 않는다.',
'',
'생성자만 명시적 inline으로 바꾼 중간 후보에서 인자 생성은 476ns로 줄었다. 실제 string_find는 8에서 1.013, 23·24에서 1.245·1.250으로, 생성자 하나만으로는 두 조건이 부족했다. find에도 inline을 지정한 뒤 세 조건이 모두 기준 안으로 들어왔다. 검색 알고리즘이나 검색 데이터는 바꾸지 않았다.',
'',
'32비트 원자 래퍼 다섯 함수와 기본 빈 훅에도 명시적 inline을 지정했다. 원자 intrinsic·메모리 순서·메모리 배치·예약 취소·hazard 보호·회수 알고리즘은 바꾸지 않았다. 전체 23개 정본 .ixx를 이전 snapshot과 대조했으며 19개는 완전히 동일하고, 네 파일은 inline 지정자를 제거하면 동일하다. 사용자 훅과 중단 테스트용 훅은 제거하지 않았다.',
'',
'| 큐 비교 소비자 오브젝트 | 32비트 원자 래퍼 call 지점 | 기본 빈 훅 call 지점 |',
'|---|---:|---:|')
foreach($r in $d.codegen){$lines+='| {0} | {1} | {2} |' -f $r.variant,$r.u32Calls,$r.emptyHookCalls}
$lines+=@('','이는 정적 기계어의 호출 지점 수이며 동적 호출 횟수가 아니다. 원자 래퍼를 inline한 진단은 720ns로 감소했고 표준 대조는 782ns였다. 생산 구현에 강제 인라이닝이나 전역 /Ob 설정 변경을 넣지 않았다. 최적화의 직접 근거는 모듈 소비자에 남았던 호출 제거와 분리 계측이다.','','## 같은 시간대의 번갈아 실행 대조','','before는 변경 전, inline은 문자열·32비트 원자 래퍼 처리 후, final은 기본 빈 훅까지 처리한 후보다. 동일 실행 파일을 재사용하고 문자열/MPSC의 반복 2 및 FAA의 짝수 반복은 후보 순서를 반전했다. 문자열/MPSC는 CPU4 측정·CPU5 자체 SMT pause helper, FAA는 기존 프로토콜 그대로 helper 없이 실행했다. 이 대조 중에는 빌드하지 않았다.','','| 문자열 길이 | 변경 전/STL 비율 범위 | 최종/STL 비율 범위 |','|---|---:|---:|')
foreach($n in 8,23,24){$a=@($d.string|Where-Object {$_.variant -eq 'before' -and $_.n -eq $n});$b=@($d.string|Where-Object {$_.variant -eq 'final' -and $_.n -eq $n});$lines+='| {0} | {1} | {2} |' -f $n,(Range $a.ratio),(Range $b.ratio)}
$lines+=@('','### FAA 8바이트·4생산자·1소비자','','| 후보 | 전송/PPL 비율 범위 | 소멸 포함/PPL 비율 범위 |','|---|---:|---:|')
foreach($v in 'before','inline','final'){$a=@($d.faa|Where-Object {$_.variant -eq $v -and $_.key -eq '8/4/1'});$lines+='| {0} | {1} | {2} |' -f $v,(Range $a.ratio),(Range $a.lifecycleRatio)}
$lines+=@('','원자 래퍼 처리만으로 개선이 확인됐으며, 빈 훅 처리의 추가 시간 이득은 이 대조에서 일관되게 분리되지 않았다. 빈 훅의 call 제거는 기계어로 확인했다. 회수 알고리즘을 바꿔 빨라졌다고 주장하지 않는다. 별도 샘플링 프로파일에서 4P1C의 allocation 호출은 6,334/6,404, reclaim_total 샘플 평균은 3,060/2,872 cycles였다. 계측 오버헤드가 있으므로 이 숫자를 실제 전송 비용과 합치거나 최종 수용값으로 사용하지 않는다.','','### MPSC 64용량·4생산자','','| 후보 | 자체 전송 시간 중앙값 범위 ms | 자체/PPL 비율 범위 | 자체 표본 편차 최대 | 작업자 시작 지연 중앙값 ns 범위 |','|---|---:|---:|---:|---:|')
foreach($v in 'before','inline','final'){
 $a=@($d.mpsc|Where-Object {$_.variant -eq $v -and $_.capacity -eq 64 -and $_.producers -eq 4})
 $ns=$a.customNs|Measure-Object -Minimum -Maximum;$s=$a.customSpread|Measure-Object -Maximum;$w=$a.workerStartNs|Measure-Object -Minimum -Maximum
 $lines+='| {0} | {1:F2}–{2:F2} | {3} | {4:F4} | {5:F0}–{6:F0} |' -f $v,($ns.Minimum/1e6),($ns.Maximum/1e6),(Range $a.ratio),$s.Maximum,$w.Minimum,$w.Maximum
}
$lines+=@('','대기 정책은 원래 OS yield를 유지했다. before에서 자체 표본 편차 1.3304가 재현됐고 최종 후보는 이 대조에서 기준 안이었다. 작업자 시작 지연의 중앙값은 모두 600–700ns로, 그 중앙값 변화가 10ms 이상 전송 차이를 설명하지는 못한다. 원시 외부 재시도 수·하위 전송 시간도 보존했다. 개별 스케줄링 지연과 과거 실패 순간의 단독 원인까지 확정한 결과는 아니다. 기본 빈 훅의 추가 효과보다 32비트 원자 경로의 호출 제거 효과가 명확하다.','','## 진행 보장과 검증','','변경은 유한 함수 호출의 인라인 지정에 한정되며 원자 연산과 상태 전이가 동일하다. 락, 스핀 대기, 생산자 등록, 할당 또는 재시도 단계를 추가하지 않았다. 고정 MPSC 코어와 FAA의 기존 진행 보장 범위를 유지한다. FAA의 CRT cold allocation 및 사용자 값 연산까지 무조건 락 프리라고 확대하지 않는다.','','- MSVC Release: 기존 헤더 및 모듈 경로 19/19.','- MSVC 모듈 AddressSanitizer: 9/9.','- Linux Clang 모듈 ThreadSanitizer: 7/7, 22개 파티션과 기본 모듈을 모두 계측해 컴파일·실행.','- 모듈 FAA·고정 MPSC의 중단/재개 검사: Release 각 20회, ASan 각 10회 반복 통과.','','예약 직후·공개 직전·공개 직후 중단, 지연 생산자의 재개, 보호 중 세그먼트 회수, 참여자 제한, 이동 전용 소유권과 정확히 한 번 소비를 포함하는 기존 검사를 재실행했다. 이 실행 검사를 수학적 lock-free 증명의 대체라고 표현하지 않는다.','','## 증거 위치','',
"독립 라이브러리 results/20261026-modopt-paired-analysis.json이 대조 분석 정본이다. cost-before/string/inline CSV, queue-before/inline/final-disasm TXT, profile-before/inline CSV 및 단계별 바이너리를 보존했다. 실제 최종 수용 입력은 $AcceptanceTag-before/environment/verdict JSON과 case CSV이며 snapshot에 수집 시점 소스·바이너리를 별도 보존했다. 대조의 final 실행 파일 경로는 후속 검증 빌드로 바뀔 수 있으므로 대조 수집 당시의 해시와 수집 종료 검사를 환경 receipt에 구분해 기록했다. 최종 수용 snapshot은 별도 바이너리 증거다.",
'',
'재현 스크립트는 run-module-opt-controls.ps1, summarize-module-opt-controls.ps1, run-module-full-suite.ps1, verify-module-acceptance.ps1이다. 처음 대조의 제어 구간 CSV에는 guard 동적 Tag 때문에 _guard 접두사가 들어갔으며 원시 이름을 유지했다. 후속 실행은 별도 controlsTag로 고쳤다. 결과나 표본을 바꾼 수정은 아니다.',
'',
'이번 판정은 위 Windows 비교 조건의 독립 라이브러리 수용이다. 엔진 통합, 실제 프레임 성능과 다른 OS의 성능 동등성은 별도 게이트다. PPL 별칭과 엔진 코드는 변경하지 않았다.'
)
$destination=Join-Path $root '../../docs/analysis/ModuleOptimizationDiagnostics20261026.md'
[IO.File]::WriteAllText($destination,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
"Wrote $destination"
