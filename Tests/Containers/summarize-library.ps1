$ErrorActionPreference='Stop'
$rows=@()
foreach($group in @('core','expanded')) {
  foreach($run in 1..3) {
    $file=Join-Path $PSScriptRoot "results\library-$group-final-$run.csv"
    if(!(Test-Path $file)){throw "Missing measurement: $file"}
    foreach($line in Get-Content $file) {
      if(!$line.StartsWith('summary,')){continue}
      $p=$line.Split(',')
      $rows += [pscustomobject]@{group=$group;run=$run;name=$p[1];n=[int]$p[2];ratio=[double]::Parse($p[6],[cultureinfo]::InvariantCulture);customSpread=[double]::Parse($p[7],[cultureinfo]::InvariantCulture);stlSpread=[double]::Parse($p[8],[cultureinfo]::InvariantCulture)}
    }
  }
}
$summary=@()
foreach($g in ($rows | Group-Object group,name,n)) {
  $items=@($g.Group | Sort-Object ratio)
  if($items.Count -ne 3){throw "Expected three process summaries: $($g.Name)"}
  $worstSpread=($items | ForEach-Object { $_.customSpread; $_.stlSpread } | Measure-Object -Maximum).Maximum
  $decision=if($items[-1].ratio -gt 1.10){'목표 초과 포함'}elseif($worstSpread -gt 1.30){'비율 충족 / 편차 재검증'}else{'독립 조건 통과'}
  $summary += [pscustomobject]@{group=$items[0].group;name=$items[0].name;n=$items[0].n;median=$items[1].ratio;min=$items[0].ratio;max=$items[-1].ratio;worstP90P10=$worstSpread;decision=$decision}
}
$summary=$summary | Sort-Object group,name,n
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 "$PSScriptRoot\results\library-summary.json"
$lines=@('# CreatorContainers 확장 구현·검증 — 2026-10-25','','정본 소스는 `../include/ce/`, 재현은 `../run-library-validation.ps1`, 원시 자료는 `library-{core,expanded}-final-{1,2,3}.csv`, 환경·SHA256은 `library-environment.json`이다. 이 보고서의 비율은 custom/STL이며 작을수록 빠르다.','','## 범위와 검증','','일반 배열·문자열·해시맵을 비단순 타입/이동 전용 값/예외 복구/사용자 할당자로 확장했다. 문자열 키 맵·셋, 세대 핸들 slot map, sequence buffer, intrusive LRU, 문자열 atom, age buckets, inplace function, 안정 32/64비트 radix sort, fixed/monotonic/TLSF resource를 추가했다. STL 컨테이너/문자열/알고리즘은 검증 실행 파일에서만 사용한다. 배포 헤더는 언어·CRT 기능을 사용한다.','','- MSVC Release: 기존 큐를 포함한 CTest 7/7.','- MSVC ASan, ClangCL Release, WSL Clang 18 Release, WSL ASan+UBSan: 일반/확장 정확성 각각 2/2.','- 공개 헤더 17개 각각 독립 컴파일: MSVC, ClangCL, WSL Clang.','- 무작위 문자열 50,000회, 문자열 키 맵 120,000회, slot map 60,000회, sequence 100,000회, LRU 100,000회, age buckets 50,000회, TLSF 100,000회. 할당 실패, 예외, 이동 전용 소유권, stale/cross-owner 핸들, 정렬 안정성도 검사한다.','','## 측정 방법과 한계','','MSVC Release / iterator debug 0, 3개 프로세스, 각 15개 표본, 3회 워밍업, A/B 순서 교대, 짧은 작업은 약 2ms 이상 배치한다. 테스트·빌드와 벤치마크는 동시에 실행하지 않는다. 사용자 앱은 그대로 둔다. 표의 중앙값은 세 프로세스 비율의 중앙값, 범위는 그 최솟값/최댓값이다. P90/P10은 양쪽 표본의 가장 큰 편차다. 세 비율이 모두 1.10 이하이고 P90/P10도 1.30 이하인 항목만 이 독립 조건 통과로 표시한다.','','문자열은 copy constructor 대 copy constructor와 pointer+length 생성 대 pointer+length 생성을 분리했다. 두 항목 모두 noinline 바이트 소비 함수로 모든 바이트를 읽으며 소비 함수 비용도 포함한다. 이전의 view 생성 대 STL copy, 또는 길이/중간 1바이트만 소비한 결과와 직접 전후 비교하지 않는다. 일반 해시맵은 tombstone 삭제로 바뀌었으며 적재율 0.5가 기본이다. 메모리 예산이 같은 STL 비교가 아니다. 슬롯/sequence/LRU/age/arena는 특정 용도의 대조다. STL과 API·메모리·실패 조건이 다르므로 일반 대체 우위로 확대하지 않는다. TLSF는 사용자 제공 arena의 128B 순차 할당/해제 대 CRT new/delete이며 초기 arena 확보 비용·실제 엔진 heap 분포는 포함하지 않는다.','','## 결과','','| 작업 | N | 중앙 비율 | 세 프로세스 범위 | 최대 P90/P10 | 판정 |','|---|---:|---:|---:|---:|---|')
$within=@($summary | Where-Object { $_.max -le 1.10 }).Count
$accepted=@($summary | Where-Object { $_.decision -eq '독립 조건 통과' }).Count
$overview="세 프로세스의 중앙 비율이 모두 1.10 이내인 조건은 $within/$($summary.Count)개다. 편차까지 포함한 현재 독립 통과 조건은 $accepted/$($summary.Count)개다. 배열 확장 65,536개는 한 프로세스에서 목표를 초과했고, 다수 항목은 활성 사용자 환경의 표본 편차로 최종 수용을 보류한다. 문자열 복사/길이 지정 생성은 모든 크기에서 비율 기준을 충족했다. 이는 엔진 통합 완료 판정이 아니다."
$lines=$lines[0..($lines.Count-3)] + @($overview,'') + $lines[($lines.Count-2)..($lines.Count-1)]
foreach($r in $summary) { $lines += '| {0} | {1} | {2:F3} | {3:F3}–{4:F3} | {5:F2} | {6} |' -f $r.name,$r.n,$r.median,$r.min,$r.max,$r.worstP90P10,$r.decision }
$lines += @('','## API·수명 계약','','배열은 noexcept move 또는 copy 경로에서 재할당 실패 시 원본 값을 보존한다. move-only throwing move와 erase 중 throwing assignment는 기본 보장이다. 중간 insert는 별도 버퍼를 사용하므로 주소를 무효화하며 항상 무할당이라고 가정하지 않는다. 맵 재해시는 noexcept key/value move 또는 양쪽 copy 가능을 요구한다. hash/equality 및 저장소와 함께 전파되는 allocator/policy 복사·대입은 noexcept여야 한다. 비복사 값 컨테이너의 복사는 컴파일 단계에서 거부한다.', '', 'string은 23B SSO이며 UTF-8 바이트·내부 NUL을 보존한다. 대소문자 변환/유니코드 정규화는 범위 밖이다. 사용자 resource는 컨테이너보다 오래 살아야 하며 fixed/monotonic reset/release는 그 저장소를 쓰는 객체를 파괴한 뒤 호출한다. allocator는 외부 컨테이너 저장소와 함께 전파하고 내부 값의 별도 allocator까지 자동 전파하지 않는다.', '', 'slot/atom 핸들은 테이블 수명 안에서만 유효하다. clear/erase 후 generation을 검사하고 다른 owner 핸들을 거부한다. generation wrap 시 슬롯을 퇴역시키거나 atom 재사용을 중단한다. sequence/age는 모듈러 uint64 wrap을 허용하지 않는다. LRU accessor/링크 저장소는 리스트보다 오래 살아야 하고 한 링크는 한 리스트에만 속한다.', '', '일반 컨테이너와 resource는 자체 동기화를 하지 않는다. TLSF는 64비트 고정 사용자 arena, 물리 이웃 병합, 64x16 비트맵, 16B 블록 정렬이며 상위 크기 클래스 반올림/fragmentation으로 실패할 수 있다. CRT로 자동 fallback하지 않는다. hash_bytes는 자체 블록 혼합으로 변경했고 영속 파일 포맷/보안 해시로 사용하지 않는다. 작은 정렬은 안정 insertion/merge, 큰 정렬은 LSD이며 noexcept 이동·대입·키 추출을 요구한다.', '', '## 수용 경계','','느린 항목과 편차 초과 항목을 평균으로 숨겨 전체 통과시키지 않는다. 엔진 PPL 별칭 및 실제 소비자는 변경하지 않았다. 락 프리 큐의 기존 성능 보고서는 별도이며 이번 일반 컨테이너 확장 결과로 대체하지 않는다. 문자열/새 타입의 보완과 실제 소비자별 메모리·프레임 수용을 마친 범위부터 적용한다.','','고정 용량 MPSC 전용 타입은 아직 제공하지 않는다. 예약 후 멈춘 생산자가 소비자를 막는 단순 sequence 링을 lock-free라고 추가하지 않았다. 현재 다중 생산자용 선택 타입은 기존 FAA lock_free_queue다. GCC/macOS/ARM64 및 OS 간 성능 행렬, 실제 엔진 씬·종료 수용은 남아 있다.')
$lines | Set-Content -Encoding utf8 "$PSScriptRoot\results\LIBRARY_REPORT.md"
Write-Output "Wrote $($summary.Count) cases"
