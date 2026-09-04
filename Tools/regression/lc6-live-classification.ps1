[CmdletBinding()]
param(
    [string]$Exe    = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work   = (Join-Path $env:TEMP "lc6live"),

    # 재 볼 명령 접두사. 비우면 registry 전체.
    [string[]]$Prefix = @('dx12', 'vk', 'render', 'rhi', 'pipeline'),

    # 명령 하나에 주는 시간. 넘으면 "라이브에서 안 끝난다"로 기록한다.
    [ValidateRange(5, 600)]
    [int]$CommandTimeoutSec = 60,

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180
)

# LC6 (PHASE 14.5) — "이 명령이 켜져 있는 에디터에서 되는가"를 **잰다**.
#
# ── 왜 재야 하는가 ──────────────────────────────────────────────────────
#
# 요구는 "특정 명령을 제외하면 에디터·플레이어를 껐다 켜지 않고 동작해야
# 한다"이다. 그러면 기본값이 라이브이고 **재시작이 필요한 것이 표시된 예외**다.
#
# 그런데 오늘 그 구분을 아무도 모른다. `Invoke-Dx12Suite.ps1` 이 프로브마다
# 에디터를 새로 띄우는 것은 사실이지만, 그것이 ① 라이브로는 안 되기 때문인지
# ② 그냥 격리를 위한 하네스 관행인지는 **한 번도 확인된 적이 없다.** 둘은
# 완전히 다른 사실이고, 추측으로 descriptor 에 적으면 그 표가 거짓말을 한다.
#
# 그래서 켜져 있는 에디터 하나에 HTTP 로 붙어 차례로 불러 보고, 무엇이 되고
# 무엇이 안 되는지를 그대로 적는다.
#
# ── 순서 오염을 가른다 ──────────────────────────────────────────────────
#
# 한 프로세스에서 연달아 부르므로, 앞 명령이 렌더러 상태를 망가뜨리면 뒤가
# 덩달아 실패한다. 그것을 "라이브 불가"로 적으면 없는 사실을 만든다. 그래서
# 1차에서 실패한 것만 **깨끗한 에디터에서 하나씩 다시** 부른다. 2차에서
# 통과하면 그 명령은 라이브 가능이고, 1차 실패는 순서 오염이었다는 뜻이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'

function Stop-AllEditors {
    Get-Process CreatorEditor -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
    if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }
}

function Start-Editor([string]$Tag) {
    Stop-AllEditors
    $p = Start-Process -FilePath $Exe -ArgumentList '--command-service' -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
        -RedirectStandardError  (Join-Path $Work "$Tag.err") -PassThru
    $until = (Get-Date).AddSeconds($BootTimeoutSec)
    while ((Get-Date) -lt $until) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $j = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($j.pid -eq $p.Id -and $j.port -gt 0) {
                    return [pscustomobject]@{
                        Process = $p
                        Base    = "http://127.0.0.1:$($j.port)"
                        Auth    = @{ Authorization = "Bearer $($j.token)" }
                    }
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 300
    }
    try { $p.Kill() } catch { }
    return $null
}

function Invoke-Command2([object]$Session, [string]$Name) {
    $body = @{ command = $Name; mode = 'sync'; timeoutMs = ($CommandTimeoutSec * 1000) } |
            ConvertTo-Json -Compress
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $r = Invoke-WebRequest ($Session.Base + '/command') -Method POST -Headers $Session.Auth `
             -ContentType 'application/json' -Body $body -UseBasicParsing `
             -SkipHttpErrorCheck -TimeoutSec ($CommandTimeoutSec + 15)
        $sw.Stop()
        $j = $r.Content | ConvertFrom-Json
        return [pscustomobject]@{
            Http    = $r.StatusCode
            Status  = $j.status
            Code    = $j.code
            Message = $j.message
            Ms      = [int]$sw.Elapsed.TotalMilliseconds
            Alive   = -not $Session.Process.HasExited
        }
    }
    catch {
        $sw.Stop()
        return [pscustomobject]@{
            Http    = 0
            Status  = 'transport_error'
            Code    = ''
            Message = $_.Exception.Message
            Ms      = [int]$sw.Elapsed.TotalMilliseconds
            Alive   = -not $Session.Process.HasExited
        }
    }
}

# ── 대상 목록을 registry 에서 뽑는다 ────────────────────────────────────
$listPath = Join-Path $Work 'registry.tsv'
if (Test-Path -LiteralPath $listPath) { Remove-Item -LiteralPath $listPath -Force }
$listScript = Join-Path $Work 'list.txt'
Set-Content -LiteralPath $listScript -Encoding UTF8 -Value @("commands.list $listPath", 'quit')
$lp = Start-Process -FilePath $Exe -ArgumentList '--script', $listScript -WorkingDirectory $exeDir `
      -RedirectStandardOutput (Join-Path $Work 'list.out') `
      -RedirectStandardError  (Join-Path $Work 'list.err') -PassThru
$lp.WaitForExit(180000) | Out-Null
if (-not (Test-Path -LiteralPath $listPath)) { "registry snapshot 실패"; exit 1 }

$targets = @([IO.File]::ReadAllLines($listPath) |
    Where-Object { $_ -and -not $_.StartsWith('#') -and -not $_.StartsWith('canonical') } |
    ForEach-Object { ($_ -split "`t")[0] } |
    # ★ `@()` 로 감싸는 것이 필수다. StrictMode 에서 파이프라인이 **하나**만
    #   돌려주면 그것은 배열이 아니라 스칼라라 `.Count` 가 없고, "속성을 찾을
    #   수 없다"로 죽는다. 접두사가 하나뿐인 호출에서만 터지므로 기본 인자로
    #   돌릴 때는 안 보인다.
    Where-Object { $n = $_; @($Prefix | Where-Object { $n.StartsWith($_ + '.') }).Count -gt 0 } |
    Sort-Object -Unique)

"대상 $($targets.Count)개 · 명령당 상한 ${CommandTimeoutSec}s"
""

$results = @{}

# ── 1차: 한 에디터에서 연달아 ───────────────────────────────────────────
$session = Start-Editor 'pass1'
if ($null -eq $session) { "1차: 서비스가 뜨지 않았다"; exit 1 }
$restarts = 0
try {
    foreach ($name in $targets) {
        if ($session.Process.HasExited) {
            # 앞 명령이 프로세스를 죽였다. 그 사실 자체가 결과다.
            $restarts++
            $session = Start-Editor "pass1_r$restarts"
            if ($null -eq $session) { break }
        }
        $r = Invoke-Command2 $session $name
        $results[$name] = $r
        "{0,-26} HTTP {1,-4} {2,-14} {3,6}ms {4}" -f `
            $name, $r.Http, $r.Status, $r.Ms, $(if ($r.Alive) { '' } else { '프로세스 종료됨' })
    }
}
finally { Stop-AllEditors }

# ── 2차: 1차 실패만 깨끗한 에디터에서 하나씩 ────────────────────────────
$suspect = @($targets | Where-Object {
    $results.ContainsKey($_) -and ($results[$_].Http -ne 200 -or $results[$_].Status -ne 'succeeded')
})
""
"2차 재검(순서 오염 분리): $($suspect.Count)개"
$clean = @{}
foreach ($name in $suspect) {
    $s = Start-Editor 'pass2'
    if ($null -eq $s) { continue }
    try {
        $r = Invoke-Command2 $s $name
        $clean[$name] = $r
        "{0,-26} HTTP {1,-4} {2,-14} {3,6}ms (단독)" -f $name, $r.Http, $r.Status, $r.Ms
    }
    finally { Stop-AllEditors }
}

# ── 판정 ────────────────────────────────────────────────────────────────
$outPath = Join-Path $Work 'live-classification.tsv'
$rows = New-Object System.Collections.Generic.List[string]
$rows.Add("command`tverdict`tpass1_http`tpass1_status`tpass1_ms`tsolo_http`tsolo_status`tsolo_ms`tmessage")

$live = 0; $orderDependent = 0; $notLive = 0
foreach ($name in $targets) {
    if (-not $results.ContainsKey($name)) { continue }
    $p1 = $results[$name]
    $p2 = if ($clean.ContainsKey($name)) { $clean[$name] } else { $null }

    $verdict =
        if ($p1.Http -eq 200 -and $p1.Status -eq 'succeeded') { $live++; 'live' }
        elseif ($null -ne $p2 -and $p2.Http -eq 200 -and $p2.Status -eq 'succeeded') { $orderDependent++; 'live-solo' }
        else { $notLive++; 'not-live' }

    $rows.Add(("{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}" -f `
        $name, $verdict, $p1.Http, $p1.Status, $p1.Ms,
        $(if ($null -ne $p2) { $p2.Http } else { '' }),
        $(if ($null -ne $p2) { $p2.Status } else { '' }),
        $(if ($null -ne $p2) { $p2.Ms } else { '' }),
        ($p1.Message -replace "`t", ' ' -replace "`r?`n", ' ')))
}
[IO.File]::WriteAllLines($outPath, $rows)

""
"live       $live"          # 연달아 불러도 그대로 된다
"live-solo  $orderDependent" # 단독이면 되는데 앞 명령에 오염된다
"not-live   $notLive"        # 단독에서도 안 된다 — 재시작이 필요한 후보
"산출: $outPath"
exit 0
