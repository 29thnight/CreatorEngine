# 크래시 덤프 경로 검증.
#
# 덤프 코드는 크래시가 나야만 실행된다. 그래서 평소에는 아무도 확인하지 않고,
# 확인되지 않은 채로 조용히 망가진다 — 실제로 로그에 CRASH 줄만 남고 .dmp는
# 통째로 없는 크래시가 있었다(Editor_20260731_102351.html).
#
# 여기서는 crash.test로 일부러 죽여서 종류별로 다음을 단정한다.
#   1. .dmp 파일이 새로 생긴다
#   2. 옆에 .txt 요약이 생기고, 그 안에 심볼이 붙은 스택이 들어 있다
#   3. 스택 맨 위쪽에 실제 크래시 지점(ConsoleCommandSystem::Execute)이 있다
#   4. 세션 로그에 기록자 등록 줄이 남는다
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$dumpDir = Join-Path $exeDir "Dump"
$logDir = Join-Path $exeDir "Log"

# 종류별 기대 요약. 각 크래시 경로가 서로 다른 후크를 타므로 전부 확인한다.
#   av        SEH 필터(미처리 예외)
#   abort     SIGABRT 시그널 핸들러
#   throw     미처리 C++ 예외 - SEH 필터가 0xE06D7363으로 먼저 받는다
$cases = @(
    @{ Kind = "av";        Expect = "ACCESS_VIOLATION" },
    @{ Kind = "abort";     Expect = "abort" },
    @{ Kind = "throw";     Expect = "C\+\+_EXCEPTION" }
)

$failures = @()

foreach ($case in $cases) {
    $kind = $case.Kind

    $script = Join-Path $Work "crash_$kind.txt"
    # wait는 프레임 단위다. 씬 로드가 끝나고 나서 죽여야 의미가 있다.
    "wait 60`ncrash.status`ncrash.test $kind`n" | Set-Content -Path $script -Encoding UTF8

    # ★ 개수 비교로 판정하면 안 된다. 엔진이 시작 시 오래된 덤프를 지우므로
    #   (kMaxRetainedDumpFiles=5) 폴더가 상한에 닿으면 '프룬 -1 + 크래시 +1'로
    #   개수가 그대로다 — 실패 잔해가 6개를 채운 순간부터 모든 실행이
    #   '생기지 않았다'로 위음성이 났다(2026-08-05 실측). 실행 시각 이후에
    #   생긴 파일을 찾는다.
    $launchTime = Get-Date

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "crash_$kind.out") `
        -RedirectStandardError (Join-Path $Work "crash_$kind.err") -PassThru
    $proc.WaitForExit(300000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); $failures += "${kind}: 타임아웃"; continue }

    # 1. 덤프가 새로 생겼는가 — 이 실행이 시작된 뒤에 쓰인 파일만 센다.
    $dump = Get-ChildItem (Join-Path $dumpDir "*.dmp") -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -gt $launchTime } |
            Sort-Object LastWriteTime | Select-Object -Last 1
    if ($null -eq $dump) {
        $failures += "${kind}: .dmp가 생기지 않았다 (실행 후 새 파일 없음)"
        continue
    }

    # 0바이트 껍데기는 '덤프는 있는데 안 열린다'로 이어지므로 크기도 본다.
    if ($dump.Length -lt 1024) {
        $failures += "${kind}: 덤프가 비었다 ($($dump.Length) 바이트)"
        continue
    }

    # 2. 요약 텍스트가 옆에 있는가
    $reportPath = [System.IO.Path]::ChangeExtension($dump.FullName, ".txt")
    if (-not (Test-Path $reportPath)) {
        $failures += "${kind}: 요약(.txt)이 없다"
        continue
    }

    $report = Get-Content $reportPath -Raw

    if ($report -notmatch $case.Expect) {
        $failures += "${kind}: 요약에 '$($case.Expect)'가 없다"
    }

    # 3. 심볼이 붙은 스택인가. '(심볼 없음)'만 늘어선 스택은 덤프가 있어도 쓸모없다.
    if ($report -notmatch 'ConsoleCommandSystem::Execute') {
        $failures += "${kind}: 스택에 크래시 지점(ConsoleCommandSystem::Execute)이 없다"
    }

    $frames = ([regex]::Matches($report, '(?m)^\s+#\d\d ')).Count
    if ($frames -lt 5) {
        $failures += "${kind}: 스택 프레임이 $frames 개뿐이다"
    }

    # 4. 이번 실행이 덤프를 남길 수 있는 상태였다는 기록
    $log = Get-ChildItem (Join-Path $logDir "Editor_*.html") |
           Sort-Object LastWriteTime | Select-Object -Last 1
    $logText = (Get-Content $log.FullName -Raw) -replace '<[^>]+>', ''
    if ($logText -notmatch '크래시 덤프 기록자 등록 완료') {
        $failures += "${kind}: 세션 로그에 기록자 등록 줄이 없다"
    }

    "{0,-10} 덤프 {1:N0} 바이트 · 스택 {2} 프레임 · {3}" -f `
        $kind, $dump.Length, $frames, $dump.Name

    # 확인이 끝났으면 지운다. 전체 메모리 덤프는 실행당 2~3GB라, 회귀를 돌릴 때마다
    # 쌓이면 금방 디스크를 채운다. 진짜 크래시의 덤프와 섞이는 것도 곤란하다.
    Remove-Item $dump.FullName -ErrorAction SilentlyContinue
    Remove-Item $reportPath -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    ""
    "실패:"
    $failures | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 ($($cases.Count)개 크래시 경로)"
exit 0
