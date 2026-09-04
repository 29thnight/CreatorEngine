[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc3"),

    # 소스 스크래핑 소비자 상한을 현재 관측으로 낮춘다. 이관이 진행됐을 때만 쓴다.
    [switch]$UpdateRatchet
)

# LC3 (PHASE 14.5) — descriptor 가 정본인가.
#
# ── 닫는 결함 ───────────────────────────────────────────────────────────
#
# `PrintHelp()` 는 손으로 쓴 문자열이었고 registry 는 별도 표였다. 둘을 잇는
# 것이 없어서 조용히 벌어졌다 — LC0 실측: 등록 205 개 중 help 에 실린 것
# **130 개(63%)**, 그리고 help 는 **등록돼 있지도 않은 이름 6 개**를 안내했다.
# 문서가 없는 것보다 틀린 문서가 나쁘고, 그 틀림을 아무도 못 알아챈 이유는
# 대조할 것이 없었기 때문이다.
#
# 이제 help 가 descriptor 에서 생성되므로 갈라질 자리가 구조적으로 없다.
# 이 게이트는 그 성질이 유지되는지, 그리고 discovery 가 소비자에게 쓸 만한
# 형태인지를 본다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$failures = New-Object System.Collections.Generic.List[string]

function Invoke-Script {
    param([string]$Name, [string[]]$Commands)
    $scriptPath = Join-Path $Work "disc_$Name.txt"
    (($Commands + 'quit') -join "`n") | Set-Content -LiteralPath $scriptPath -Encoding UTF8
    $outPath = Join-Path $Work "disc_$Name.out"
    $proc = Start-Process -FilePath $Exe -ArgumentList '--script', $scriptPath -WorkingDirectory $exeDir `
        -RedirectStandardOutput $outPath -RedirectStandardError (Join-Path $Work "disc_$Name.err") -PassThru
    $proc.WaitForExit(180000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); return $null }
    [pscustomobject]@{ ExitCode = $proc.ExitCode; Stdout = (Get-Content -LiteralPath $outPath -Raw -Encoding UTF8) }
}

# ── 1) registry 무결성 ──────────────────────────────────────────────────
#
# 이름 중복·요약 누락·descriptor 부재를 엔진이 스스로 판정한다. 예전에는
# 이름 중복이 printf 한 줄로 지나가고 그 뒤로 조용히 한쪽이 먹혔다.
$selftest = Invoke-Script 'selftest' @('commands.selftest')
if ($null -eq $selftest) { $failures.Add('selftest : 타임아웃') }
else {
    $line = ($selftest.Stdout -split "`n" | Where-Object { $_ -match '\[commands\.selftest\] 명령=' } | Select-Object -First 1)
    "registry                 $($line -replace '^\[commands\.selftest\] ','')".TrimEnd()
    if ($selftest.ExitCode -ne 0) {
        $failures.Add("selftest : exit $($selftest.ExitCode) — registry 무결성 위반이 있다")
        $selftest.Stdout -split "`n" | Where-Object { $_ -match '문제:' } | ForEach-Object { "  $($_.Trim())" }
    }
}

# ── 2) help 와 registry 가 같은 집합이다 ────────────────────────────────
#
# 커버리지(등록 → help)와 고아(help → 등록) **양방향**을 본다. 한 방향만 보면
# LC0 이 찾은 결함 둘 중 하나를 놓친다.
$both = Invoke-Script 'help_and_list' @('help', 'commands.list')
if ($null -eq $both) { $failures.Add('help/list : 타임아웃') }
else {
    $lines = $both.Stdout -split "`n"

    $registered = @($lines |
        Where-Object { $_ -notmatch '^#' -and $_ -notmatch '^canonical\t' -and $_ -match "`t" } |
        ForEach-Object { ($_ -split "`t")[0].Trim() } |
        Where-Object { $_ -match '^[a-z][a-z0-9._]*$' } |
        Sort-Object -Unique)

    # help 본문의 명령 줄: 두 칸 들여쓰기 + 이름(|별칭)
    $helped = @($lines |
        Where-Object { $_ -match '^  [a-z][a-z0-9._]*(\|[a-z0-9._]+)*(\s|$)' } |
        ForEach-Object { (($_.Trim() -split '\s+')[0] -split '\|')[0] } |
        Sort-Object -Unique)

    "help                     등록 $($registered.Count) · help 게재 $($helped.Count)"

    if ($registered.Count -eq 0) { $failures.Add('help/list : discovery 출력을 못 읽었다') }
    else {
        $missing = @($registered | Where-Object { $helped -notcontains $_ })
        $orphan  = @($helped | Where-Object { $registered -notcontains $_ })
        if ($missing.Count -gt 0) {
            $failures.Add("help coverage : help 에 없는 등록 명령 $($missing.Count)개 — $(($missing | Select-Object -First 6) -join ', ')")
        }
        if ($orphan.Count -gt 0) {
            $failures.Add("help orphan : 등록되지 않은 이름을 help 가 안내한다 $($orphan.Count)개 — $(($orphan | Select-Object -First 6) -join ', ')")
        }
    }
}

# ── 3) snapshot 이 결정적이다 ───────────────────────────────────────────
#
# 소비자가 diff 로 비교하려면 실행마다 같은 순서여야 한다. 등록 순서나 해시
# 순회에 기대면 흔들린다(§13 LC3 완료 기준).
$a = Invoke-Script 'det_a' @('commands.list')
$b = Invoke-Script 'det_b' @('commands.list')
if ($null -eq $a -or $null -eq $b) { $failures.Add('determinism : 타임아웃') }
else {
    $rows = { param($t) @(($t -split "`n") | Where-Object { $_ -match "`t" -and $_ -notmatch '^#' }) }
    $ra = & $rows $a.Stdout
    $rb = & $rows $b.Stdout
    "determinism              $($ra.Count) 행 · 두 실행 비교"
    if ($ra.Count -eq 0) { $failures.Add('determinism : snapshot 이 비어 있다') }
    elseif (Compare-Object $ra $rb) { $failures.Add('determinism : 두 실행의 snapshot 이 다르다') }
}

# ── 4) 소비자가 C++ 소스에서 명령 목록을 뽑지 않는다 ────────────────────
#
# LC3 이 discovery 를 열었으므로 이 수는 줄기만 해야 한다. 늘면 붉어진다.
$ratchetPath = Join-Path $PSScriptRoot 'cli_discovery.ratchet.json'
$ratchet = Get-Content -LiteralPath $ratchetPath -Raw | ConvertFrom-Json

$scrapers = New-Object System.Collections.Generic.List[string]
foreach ($root in @('Tools', 'scripts')) {
    $full = Join-Path $repoRoot $root
    if (-not (Test-Path -LiteralPath $full -PathType Container)) { continue }
    foreach ($file in (Get-ChildItem -LiteralPath $full -Recurse -File -Include *.ps1, *.txt, *.py, *.md)) {
        $name = $file.Name
        if ($name -like 'lc0-*' -or $name -like 'verify-cli-*') { continue }
        # ★ 주석에서 파일 이름을 언급하는 것은 스크래핑이 아니다.
        #
        #   처음에는 `ConsoleCommandSystem.cpp` 를 담은 파일을 전부 셌는데,
        #   11 개 중 5 개는 "예전에 이 파일의 이 함수가 이랬다"는 **주석**이었다.
        #   고칠 수 없는 것을 눈금에 넣으면 래칫이 0 에 닿지 못하고, 닿지 못하는
        #   눈금은 아무도 안 본다. 경로를 **데이터로** 쓰는 줄만 센다.
        $hits = @(Select-String -LiteralPath $file.FullName -Pattern 'ConsoleCommandSystem\.cpp' |
                  Where-Object { $_.Line -notmatch '^\s*#' } |
                  Where-Object { $_.Line -match 'Path|Get-Content|LiteralPath' })
        if ($hits.Count -gt 0) {
            $scrapers.Add(($file.FullName.Substring($repoRoot.Length + 1) -replace '\\', '/'))
        }
    }
}

"source-scraping          $($scrapers.Count) 파일 / 상한 $($ratchet.maxSourceScrapers)"
foreach ($s in ($scrapers | Sort-Object)) { "  $s" }

if ($scrapers.Count -gt $ratchet.maxSourceScrapers) {
    $failures.Add("source-scraping : C++ 소스를 긁는 소비자가 늘었다 $($scrapers.Count) > $($ratchet.maxSourceScrapers). 명령 목록은 commands.list 에서 가져와라")
}
elseif ($scrapers.Count -lt $ratchet.maxSourceScrapers) {
    if ($UpdateRatchet) {
        $ratchet.maxSourceScrapers = $scrapers.Count
        $ratchet | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ratchetPath -Encoding UTF8
        "래칫 상한을 $($scrapers.Count) 로 낮췄다."
    }
    else { "이관이 진행됐다. -UpdateRatchet 으로 상한을 낮춰라." }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"discovery 전체 통과"
exit 0
