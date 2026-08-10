# UI 회귀 세트 전체 실행.
#
# 각 검사는 종료 코드로 판정한다. 하나라도 실패하면 이 스크립트도 실패로 끝난다.
#
#   ui_regression.txt         비정상 순서로 UI를 만들고 재생/정지를 반복한다(PHASE 6).
#                             에디터에서 정상 순서로 만들면 드러나지 않는 크래시를 잡는다.
#   verify-authored-rects     런타임이 계산한 배치가 에디터가 저장해 둔 배치와 같은지(7-2·7-4).
#   verify-resolution-sweep   해상도를 바꿔 가며 캔버스·자식·클릭 판정이 따라오는지(7-1·7-3·7-6).
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$failed = @()

function Run-Step([string]$name, [scriptblock]$body) {
    "=== $name ==="
    & $body
    if ($LASTEXITCODE -ne 0) { $script:failed += $name }
    ""
}

Run-Step "UI 생성 순서 회귀" {
    $script = Join-Path $PSScriptRoot "ui_regression.txt"
    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "ui_regression.out") `
        -RedirectStandardError (Join-Path $Work "ui_regression.err") -PassThru
    $proc.WaitForExit(300000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; $global:LASTEXITCODE = 1; return }

    # UiTextProbe가 로그에 통과/실패를 남긴다. 종료 코드만으로는 단정 실패를 알 수 없다.
    $log = Get-ChildItem (Join-Path $exeDir "Log\Editor_*.html") |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $text = (Get-Content $log.FullName -Raw) -replace '<[^>]+>', ''
    $passes = ([regex]::Matches($text, 'UiTextProbe\] 전체 통과')).Count
    $fails = ([regex]::Matches($text, 'UiTextProbe\] .*실패')).Count
    $crash = $text -match '미처리 예외'

    "통과 $passes 회 · 실패 $fails 회 · 종료 코드 $($proc.ExitCode)"
    if ($crash) { "크래시가 기록됐다" }
    # 종료 코드도 판정에 넣는다 — 프로브가 다 통과하고도 종료 시점 힙 손상
    # (0xC0000374)으로 죽은 실행이 '전체 통과'로 지나간 적이 있다. 간헐 크래시는
    # 검사를 흔들리게 만들지만, 숨겨지는 것보다 흔들리는 쪽이 낫다.
    $exitOk = ($proc.ExitCode -eq 0)
    if (-not $exitOk) { ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }
    $global:LASTEXITCODE = if ($passes -ge 4 -and $fails -eq 0 -and -not $crash -and $exitOk) { 0 } else { 1 }
}

Run-Step "저작 배치 재현" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-authored-rects.ps1") -Exe $Exe -Work $Work
}

Run-Step "해상도 스위프" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-resolution-sweep.ps1") -Exe $Exe -Work $Work
}

Run-Step "종료 순서" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-shutdown-order.ps1") -Exe $Exe -Work $Work
}

Run-Step "크래시 덤프 경로" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-crash-dump.ps1") -Exe $Exe -Work $Work
}

# 행동 트리 동작(PHASE 9-8).
#
# 이 세트의 나머지 검사는 BT를 전혀 실행하지 않는다 — BT 컴포넌트는 프리팹에만
# 붙어 있고 다른 시나리오가 여는 씬에는 없다. 그래서 이 항목이 없으면 세트 전체가
# 통과해도 BT 코드는 한 줄도 돌지 않은 채 통과한다. "안 깨졌다"와 "동작한다"를
# 가르는 자리다.
Run-Step "행동 트리 동작" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-bt-smoke.ps1") -Exe $Exe -Work $Work
}

# 프리팹 왕복(트랙 P).
#
# 다른 검사는 전부 한 번 띄운 상태만 본다. 저장했다 다시 여는 왕복이 없어서
# "인스턴스가 프리팹과의 연결을 잃는다"는 회귀가 통째로 사각지대였다.
#
# 이 검사는 지금 '부분 통과'로 끝난다 — 왕복 전 연결은 정상이고 왕복 후 복원만
# 미구현이라(P-a) 그 항목을 예상된 실패로 구분해 보고한다. P2에서 복원이 서면
# -Strict를 기본으로 올려 완전 통과를 요구한다.
Run-Step "프리팹 왕복" {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-prefab-roundtrip.ps1") -Exe $Exe -Work $Work
}

# 생명주기 순서 대조(PHASE 9-0)는 기준선 파일이 있을 때만 돈다.
# 기준선을 뜨려면 PHASE 9 교체 전에 한 번:
#   .\verify-lifecycle-baseline.ps1 -Baseline
# 기준선이 없는데 실패로 처리하면, 이 항목을 아직 시작하지 않은 사람에게
# 회귀 세트가 통째로 빨갛게 보인다 — 그러면 세트 전체가 무시되기 시작한다.
if (Test-Path (Join-Path $PSScriptRoot "lifecycle_baseline.tsv")) {
    Run-Step "생명주기 순서" {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot "verify-lifecycle-baseline.ps1") -Exe $Exe -Work $Work
    }
} else {
    "=== 생명주기 순서 === 건너뜀 (기준선 없음 — verify-lifecycle-baseline.ps1 -Baseline)"
    ""
}

if ($failed.Count -gt 0) {
    "실패한 검사: " + ($failed -join ', ')
    exit 1
}
"회귀 세트 전체 통과"
exit 0
