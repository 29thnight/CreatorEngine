param([string]$Tag='20261026-mpsc-sampler')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results';$culture=[cultureinfo]::InvariantCulture
function Num([string]$s){[double]::Parse($s,$culture)}
function Median($v){$a=@($v|Sort-Object);$i=[int][math]::Floor($a.Count/2);if($a.Count%2){$a[$i]}else{($a[$i-1]+$a[$i])/2}}
function Spread($v){$a=@($v|Sort-Object);$a[[int][math]::Floor($a.Count*.9)]/$a[[int][math]::Floor($a.Count*.1)]}
$points=@();$verifiedPairs=0
$modes=@('yield-single','start-pause-single','pause-single','yield-batch8','pause-batch8')
foreach($mode in $modes){foreach($run in 1..3){
 $lines=@(Get-Content "$out/$Tag-$mode-$run.csv"|ForEach-Object {,$_.Split(',')})
 $policy=@($lines|Where-Object {$_[0] -eq 'mpsc_policy'})
 if($policy.Count -ne 1){throw 'Missing policy'}
 $repeat=[int]$policy[0][6]
 $expected=if($mode -like '*batch8'){8}else{1}
 if($repeat -ne $expected){throw 'Wrong batch size'}
 $summaries=@($lines|Where-Object {$_[0] -eq 'mpsc_summary'})
 if($summaries.Count -ne 4){throw 'Incomplete four-case process'}
 foreach($summary in $summaries){
  $cap=[int]$summary[1];$p=[int]$summary[2]
  $sub=@($lines|Where-Object {$_[0] -eq 'mpsc_subraw' -and [int]$_[1] -eq $cap -and [int]$_[2] -eq $p -and [int]$_[3] -ge 0})
  $warm=@($lines|Where-Object {$_[0] -eq 'mpsc_subraw' -and [int]$_[1] -eq $cap -and [int]$_[2] -eq $p -and [int]$_[3] -lt 0})
  if($sub.Count -ne 15*$repeat -or $warm.Count -ne 3*$repeat){throw 'Incomplete retained/predefined warmup trials'}
  $a=@();$b=@()
  foreach($sample in 0..14){
   $parts=@($sub|Where-Object {[int]$_[3] -eq $sample})
   if($parts.Count -ne $repeat -or @($parts|ForEach-Object {$_[4]}|Sort-Object -Unique).Count -ne $repeat){throw 'Missing/duplicate sub-transfer'}
   $x=($parts|ForEach-Object {Num $_[5]}|Measure-Object -Sum).Sum/$repeat
   $y=($parts|ForEach-Object {Num $_[6]}|Measure-Object -Sum).Sum/$repeat
   $raw=@($lines|Where-Object {$_[0] -eq 'mpsc_raw' -and [int]$_[1] -eq $cap -and [int]$_[2] -eq $p -and [int]$_[3] -eq $sample})
   if($raw.Count -ne 1 -or [math]::Abs($x-(Num $raw[0][4])) -gt 1 -or [math]::Abs($y-(Num $raw[0][5])) -gt 1){throw 'Batch mean differs from retained sub-transfers'}
   $a+=$x;$b+=$y
  }
  $verifiedPairs+=$sub.Count
  $ratio=(Median $a)/(Median $b);$sa=Spread $a;$sb=Spread $b
  if([math]::Abs($ratio-(Num $summary[5])) -gt .00015 -or [math]::Abs($sa-(Num $summary[6])) -gt .00015 -or [math]::Abs($sb-(Num $summary[7])) -gt .00015){throw 'Summary differs from raw data'}
  $points+=[pscustomobject]@{mode=$mode;run=$run;capacity=$cap;producers=$p;repeat=$repeat;ratio=$ratio;aSpread=$sa;bSpread=$sb;aNs=(Median $a);bNs=(Median $b);aSingleSpread=(Spread @($sub|ForEach-Object {Num $_[5]}));bSingleSpread=(Spread @($sub|ForEach-Object {Num $_[6]}));aPushRetries=($sub|ForEach-Object {Num $_[7]}|Measure-Object -Average).Average;aEmptyRetries=($sub|ForEach-Object {Num $_[8]}|Measure-Object -Average).Average;bEmptyRetries=($sub|ForEach-Object {Num $_[10]}|Measure-Object -Average).Average;aStartNs=(Median @($sub|ForEach-Object {Num $_[11]}));bStartNs=(Median @($sub|ForEach-Object {Num $_[12]}));aStartMax=($sub|ForEach-Object {Num $_[11]}|Measure-Object -Maximum).Maximum;bStartMax=($sub|ForEach-Object {Num $_[12]}|Measure-Object -Maximum).Maximum}
 }
}}
$groups=@(foreach($g in ($points|Group-Object mode,capacity,producers)){
 $v=@($g.Group);if($v.Count -ne 3){throw 'Missing process repeat'}
 $r=($v.ratio|Measure-Object -Maximum).Maximum
 $s=($v|ForEach-Object {$_.aSpread;$_.bSpread}|Measure-Object -Maximum).Maximum
 [pscustomobject]@{mode=$v[0].mode;capacity=$v[0].capacity;producers=$v[0].producers;ratio=(Median $v.ratio);minRatio=($v.ratio|Measure-Object -Minimum).Minimum;maxRatio=$r;spread=$s;accepted=($r -le 1.10 -and $s -le 1.30);rows=$v}
})
$final=@($groups|Where-Object {$_.mode -eq 'yield-batch8'})
$accepted=@($final|Where-Object accepted).Count
$receipt=Get-Content "$out/$Tag-environment.json" -Raw|ConvertFrom-Json
foreach($file in @($receipt.sources)+@($receipt.scripts)+@($receipt.binaries)){if((Get-FileHash $file.Path -Algorithm SHA256).Hash -ne $file.Hash){throw "Source/binary changed: $($file.Path)"}}
$sanitized=Get-Content "$out/20261026-final2-environment.json" -Raw|ConvertFrom-Json
$old=@{};foreach($file in $sanitized.sources){$old[$file.Path]=$file.Hash}
$headers=@($receipt.sources|Where-Object {$_.Path -like '*\include\*'})
foreach($file in $headers){if($old[$file.Path] -ne $file.Hash){throw 'Sanitizer-tested library header differs'}}
$general=Get-Content "$out/20261026-final5-environment.json" -Raw|ConvertFrom-Json
foreach($target in @('container_tests','library_benchmark')){$file=@($general.binaries|Where-Object {[IO.Path]::GetFileNameWithoutExtension($_.Path) -eq $target});if($file.Count -ne 1 -or (Get-FileHash $file[0].Path).Hash -ne $file[0].Hash){throw 'General benchmark binary changed'}}
$result=[ordered]@{tag=$Tag;points=$points;cases=$groups;final=$final;accepted=$accepted;total=4;pass=($accepted -eq 4);verifiedSubTransferPairs=$verifiedPairs;sanitizerHeadersMatched=$headers.Count;protocol='yield-batch8 predefined before collection; all raw sub-transfers retained; mean ns per original transfer'}
$result|ConvertTo-Json -Depth 9|Set-Content "$out/$Tag-summary.json" -Encoding utf8
$text=@('# MPSC 대기·표본 길이 분리 계측 — 2026-10-26','',"최종 사전 지정 조건 yield-batch8: **$accepted/4**. 프로세스 3회 모두 custom/PPL 중앙값 ≤1.10, 양쪽 15표본 x[13]/x[1] ≤1.30 기준이다. $verifiedPairs 쌍의 정식 개별 전송을 보존·대조했다. 공개 헤더 $($headers.Count)개가 sanitizer 검사본과 같다.",'','각 전송은 기존과 같은 128B 값, 생산자별 262,144개다. 큐/스레드 생성은 시간 밖, ready barrier 뒤 start부터 join까지는 시간 안이다. yield-batch8은 기존 OS yield 대기 정책 그대로 8개 전송의 시간 합을 8로 나눈 작업량 정규화 표본이다. 연속 단일 wall interval이 아니며, 단일 전송 지연 편차나 엔진 p99를 통과했다는 뜻은 아니다. 3개 고정 워밍업 묶음과 15개 정식 묶음을 쓰고 모든 하위 전송 시간·재시도 횟수·작업자 시작 지연을 남겼다.','','모든 모드는 같은 계측 바이너리다. yield-single → 시작 대기만 pause32 → 시작/재시도 모두 pause32로 나누고, 8회 묶음을 각각 추가했다. 반복 2는 모드 순서를 반전했다. pause는 실패/대기마다 CPU pause 32회이며 큐 코어 변경이 아니다. 외부 push 실패는 strict full뿐 아니라 예약/경합도 포함한다. 외부 재시도 수를 PPL 내부 동기화 횟수로 해석하지 않는다. 시작 지연은 start 공개부터 작업자가 대기를 벗어난 시각까지다. 재시도·시작 지연의 상관만으로 OS/큐 내부 비용을 완전히 분해했다고 주장하지 않는다. 전용 코어의 busy polling 대조는 엔진 CPU 전력·수면 정책의 수용이 아니다.','','| 모드 | 용량 | 생산자 | 중앙 비율 | 프로세스 범위 | 최대 표본 편차 | 수용 |','|---|---:|---:|---:|---:|---:|---|')
foreach($r in ($groups|Sort-Object mode,capacity,producers)){$text+='| {0} | {1} | {2} | {3:F3} | {4:F3}–{5:F3} | {6:F3} | {7} |' -f $r.mode,$r.capacity,$r.producers,$r.ratio,$r.minRatio,$r.maxRatio,$r.spread,$r.accepted}
$text+=@('','| 모드, 용량, 생산자 | 자체 push 재시도/전송 | 자체 empty 재시도/전송 | PPL empty 재시도/전송 | 자체 시작 ns 중앙값 | PPL 시작 ns 중앙값 | 단일 자체 전송 최대 P90/P10 |','|---|---:|---:|---:|---:|---:|---:|')
foreach($r in ($groups|Sort-Object mode,capacity,producers)){$v=$r.rows;$text+='| {0}, {1}, {2} | {3:F0} | {4:F0} | {5:F0} | {6:F0} | {7:F0} | {8:F3} |' -f $r.mode,$r.capacity,$r.producers,(Median $v.aPushRetries),(Median $v.aEmptyRetries),(Median $v.bEmptyRetries),(Median $v.aStartNs),(Median $v.bStartNs),($v.aSingleSpread|Measure-Object -Maximum).Maximum}
$text|Set-Content "$out/MPSC_SAMPLER_REPORT_20261026.md" -Encoding utf8
"MPSC_SAMPLER_VERIFIED: final $accepted/4; $verifiedPairs retained sub-transfer pairs; $($headers.Count) sanitizer headers match."
