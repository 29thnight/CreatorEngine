[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc1"),

    # 정적 래칫의 상한을 현재 관측으로 낮춘다. 이행이 진행돼 수가 줄었을 때만 쓴다.
    [switch]$UpdateRatchet
)

# LC1 (PHASE 14.5) — exit spine 의 동적 판정 + 이행 래칫.
#
# verify-cli-exit-contract.ps1 이 "실패가 종료 코드로 나오는가"를 보는 반면,
# 이 게이트는 그 반대편 셋을 본다.
#
#   ① 성공만 있는 실행은 0 이어야 한다. 실패를 잡느라 성공을 비-0 으로 만드는
#      회귀가 가장 흔한 과잉 교정이고, 그러면 세트 전체가 붉어져 아무도 안 본다.
#   ② `--fail-fast` 가 실제로 남은 명령을 버린다.
#   ③ 직접 `EngineBootstrap::SetExitCode` 호출 수가 **늘지 않는다**.
#
# ③ 이 정적 래칫이다. 계획 §14.1 의 최종 상태는 "session adapter 한 곳"이고,
# 오늘은 거기까지 가지 않았다 — LC1 은 대표 6 개만 이행했고 나머지는 legacy 다.
# 그래서 "0 이어야 한다"가 아니라 **"오늘 수보다 많아지면 붉어진다"**로 둔다.
# 한 방향으로만 도는 래칫이라, 이행이 끝나면 상한이 0 에 닿는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$failures = New-Object System.Collections.Generic.List[string]

# ── ③ 정적 래칫 ─────────────────────────────────────────────────────────
#
# session adapter(CommandSession.cpp) 자신은 세지 않는다 — 거기가 유일하게
# 허용된 자리다. 그 파일에서 부르지 않으면 exit code 를 쓸 곳이 아예 없다.
$ratchetPath = Join-Path $PSScriptRoot 'cli_exit_spine.ratchet.json'
$ratchet = Get-Content -LiteralPath $ratchetPath -Raw | ConvertFrom-Json

$sessionAdapter = 'Editor/EngineEntry/CommandCore/CommandSession.cpp'
$scanRoots = @('Editor', 'Player', 'Engine') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }

$directWrites = New-Object System.Collections.Generic.List[object]
foreach ($root in $scanRoots) {
    foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File -Include *.cpp, *.h)) {
        $relative = ($file.FullName.Substring($repoRoot.Length + 1) -replace '\\', '/')
        if ($relative -eq $sessionAdapter) { continue }

        # 선언부(EngineBootstrap.h)는 호출이 아니다.
        if ($relative -eq 'Editor/EngineEntry/EngineBootstrap.h') { continue }

        $hits = Select-String -LiteralPath $file.FullName -Pattern 'EngineBootstrap::SetExitCode\('
        foreach ($hit in $hits) {
            $directWrites.Add([pscustomobject]@{ file = $relative; line = $hit.LineNumber })
        }
    }
}

$byFile = $directWrites | Group-Object file | Sort-Object Name
"직접 SetExitCode 호출 (session adapter 제외): $($directWrites.Count) 곳 / 상한 $($ratchet.maxDirectExitWrites)"
foreach ($group in $byFile) { "  $($group.Name): $($group.Count)" }

if ($directWrites.Count -gt $ratchet.maxDirectExitWrites) {
    $failures.Add(("직접 SetExitCode 호출이 늘었다: $($directWrites.Count) > 상한 $($ratchet.maxDirectExitWrites). " +
                   "새 명령은 CommandResult 를 반환해야 한다 — exit code 를 직접 쓰면 뒤의 성공이 앞의 실패를 지운다."))
}
elseif ($directWrites.Count -lt $ratchet.maxDirectExitWrites) {
    if ($UpdateRatchet) {
        $ratchet.maxDirectExitWrites = $directWrites.Count
        $ratchet | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ratchetPath -Encoding UTF8
        "래칫 상한을 $($directWrites.Count) 로 낮췄다."
    }
    else {
        "이행이 진행됐다($($directWrites.Count) < $($ratchet.maxDirectExitWrites)). -UpdateRatchet 으로 상한을 낮춰라."
    }
}

# ── 동적 판정 ───────────────────────────────────────────────────────────
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe — 정적 래칫만 판정했다."
    if ($failures.Count -gt 0) { $failures | ForEach-Object { "  - $_" }; exit 1 }
    exit 0
}
$exeDir = Split-Path -Parent $Exe

function Invoke-Scenario {
    param([string]$Name, [string[]]$Commands, [string[]]$ExtraArgs = @())

    $scriptPath = Join-Path $Work "spine_$Name.txt"
    (($Commands + 'quit') -join "`n") | Set-Content -LiteralPath $scriptPath -Encoding UTF8

    $outPath = Join-Path $Work "spine_$Name.out"
    $arguments = @('--script', $scriptPath) + $ExtraArgs
    $proc = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory $exeDir `
        -RedirectStandardOutput $outPath `
        -RedirectStandardError (Join-Path $Work "spine_$Name.err") -PassThru
    $proc.WaitForExit(180000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); return $null }

    [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Stdout   = (Get-Content -LiteralPath $outPath -Raw -ErrorAction SilentlyContinue)
    }
}

function Assert-Exit {
    param([string]$Name, $Result, [int]$Expected, [string]$Why)
    if ($null -eq $Result) { $script:failures.Add("$Name : 타임아웃"); return }
    if ($Result.ExitCode -ne $Expected) {
        $script:failures.Add("$Name : exit $($Result.ExitCode) ≠ 기대 $Expected — $Why")
    }
    "{0,-24} exit={1} (기대 {2})" -f $Name, $Result.ExitCode, $Expected
}

# ① 성공만 있는 실행은 0 이다.
#
#    이행한 명령(help/wait/scene.load…)과 legacy 명령을 섞는다. legacy 는
#    LegacyUnreported 를 내는데 그것이 exit 0 으로 사상되지 않으면 아직
#    이행되지 않은 명령 199 개가 전부 실패로 뒤집힌다.
Assert-Exit 'all-success' (Invoke-Scenario 'all_success' @('help', 'wait 2', 'crash.status')) 0 `
    'LegacyUnreported 가 exit 0 으로 사상되지 않으면 미이행 명령이 전부 실패가 된다'

# ★ legacy 핸들러의 직접 exit 쓰기가 살아남는가.
#
#   이 케이스가 없어서 실제 회귀를 놓쳤다. session 이 매 명령마다 exit code 를
#   쓰기 시작하자, 아직 이행되지 않은 핸들러가 직접 쓴 값이 같은 프레임 안에서
#   0 으로 덮였다 — `material.corpus.probe` 를 인자 없이 부르면 핸들러가 6 을
#   쓰는데 프로세스는 0 으로 끝났다. LC1 이 고치려던 결함을 LC1 이 더 나쁜
#   형태로 만든 자리였고, 정적 래칫은 호출 **수**만 세므로 잡지 못했다.
#
#   래칫이 0 에 닿을 때까지(=legacy 직접 쓰기가 남아 있는 동안) 이 케이스를 둔다.
Assert-Exit 'legacy-direct-exit' (Invoke-Scenario 'legacy_direct' @('material.corpus.probe')) 4 `
    'legacy 핸들러의 SetExitCode(6)가 session 에 흡수돼 §5.4 의 4 로 나와야 한다'

# 인자 오류.
Assert-Exit 'wait-garbage' (Invoke-Scenario 'wait_garbage' @('wait abc')) 2 `
    'atoi 가 실패를 0 으로 삼키던 자리 — 이제 InvalidArguments 다'

Assert-Exit 'wait-negative' (Invoke-Scenario 'wait_negative' @('wait -5')) 2 `
    'max(0,...) 가 음수를 조용히 0 으로 바꾸던 자리'

# ② --fail-fast 가 남은 명령을 버린다.
#
#    표식으로 cli.echo.args 를 쓴다. fail-fast 가 동작하면 실패 뒤의 표식이
#    출력에 없어야 한다. 종료 코드만 보면 버렸는지 계속 돌았는지 구분되지 않는다.
$failFast = Invoke-Scenario 'fail_fast' `
    @('scene.load Assets/Scenes/NoSuchScene.creator', 'cli.echo.args LC1MARKER') @('--fail-fast')
Assert-Exit 'fail-fast' $failFast 3 '선행조건 실패가 종료 코드로 나와야 한다'
if ($null -ne $failFast -and $failFast.Stdout -match 'LC1MARKER') {
    $failures.Add('fail-fast : 실패 뒤의 명령이 그대로 실행됐다 — 큐를 버리지 않았다')
}

# 기본은 continue + aggregate 다. 같은 시나리오를 플래그 없이 돌리면 뒤 명령이 돈다.
$continueRun = Invoke-Scenario 'continue' `
    @('scene.load Assets/Scenes/NoSuchScene.creator', 'cli.echo.args LC1MARKER')
Assert-Exit 'continue-aggregate' $continueRun 3 '실패는 보존하되 뒤 명령은 계속 돈다'
if ($null -ne $continueRun -and $continueRun.Stdout -notmatch 'LC1MARKER') {
    $failures.Add('continue-aggregate : 기본 모드인데 뒤의 명령이 실행되지 않았다')
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"exit spine 전체 통과"
exit 0
