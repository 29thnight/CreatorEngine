[CmdletBinding()]
param(
    # 패키지된 Player 번들. 비우면 `Build\Staging\<프로젝트>.current.json` 이 가리키는
    # 릴리스 디렉터리를 쓴다.
    [string]$Stage = '',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180
)

# PHASE 14.5 LC8 (§11.2 · §11.3) — 실행 중인 Development Player 에 붙는다.
#
# ── 무엇을 단정하는가 ───────────────────────────────────────────────────
#
#   ① `/health` 가 `role: "player"` 를 낸다 — 붙은 것이 Player registry 다.
#   ② `/commands` 가 Player role 을 가진 이름만 낸다.
#   ③ 상태를 읽는다(씬 이름·오브젝트 수·오브젝트 하나의 트랜스폼).
#   ④ **런타임 명령이 재시작 없이 반영된다** — `player.move` 로 옮기고 되읽는다.
#   ⑤ Editor 전용 명령이 **부재**한다(404 command.unknown). 런타임 거부가 아니다.
#
# ── ④ 가 이 계층의 값어치다 ────────────────────────────────────────────
#
# §11.3: "재현이 어려운 상태에 도달한 뒤 그 상태 위에서 명령을 시험할 수 있다는
# 것이 이 계층의 실제 값어치다." 쓰기만 확인하면 "명령이 성공했다" 까지밖에 못
# 말한다. 되읽어야 **반영**을 말할 수 있다.
#
# ── ⑤ 를 코드가 아니라 상태로 본다 ─────────────────────────────────────
#
# 404 여야 한다. 403·409 는 "있는데 지금은 안 된다" 이고, §11.2 가 요구한 것은
# "애초에 등록되지 않는다" 다. 두 상태를 섞으면 discovery 를 보는 에이전트가
# 나중에 다시 시도해야 하는지 아닌지를 알 수 없다.
#
# ── Player 는 패키지된 호스트다 ─────────────────────────────────────────
#
# `Bin\...\Player\Player.exe` 를 그대로 띄우면 런타임 콘텐츠 준비에서 실패한다
# (GameAssets.pak 부재 · exit 5). 그래서 이 게이트는 **스테이지된 번들**을 쓰고,
# 거기에 방금 지은 Player.exe 와 런타임 DLL 만 덮어 쓴다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$binRoot  = Join-Path $repoRoot "Bin\x64-$Config\Player"

if ([string]::IsNullOrWhiteSpace($Stage)) {
    $current = Join-Path $repoRoot 'Build\Staging\Dynamic_CPP.current.json'
    if (-not (Test-Path -LiteralPath $current -PathType Leaf)) {
        "스테이지된 패키지가 없다: $current"
        "  먼저 한 번 패키지를 만들 것: pwsh Tools/build.ps1 -Config $Config -InputMode Project -BuildNative"
        exit 1
    }
    $manifest = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
    $Stage = Join-Path $repoRoot "Build\Staging\$($manifest.releaseDirectory)"
}
if (-not (Test-Path -LiteralPath (Join-Path $Stage 'GameAssets.pak') -PathType Leaf)) {
    "스테이지에 GameAssets.pak 이 없다: $Stage"; exit 1
}
if (-not (Test-Path -LiteralPath (Join-Path $binRoot 'Player.exe') -PathType Leaf)) {
    "Player 를 먼저 지을 것: $binRoot\Player.exe"; exit 1
}

# 스테이지를 건드리지 않는다 — 사본에 덮어쓴다. 원본은 다른 검사가 쓴다.
$work = Join-Path $env:TEMP "lc8-player-$Config"
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
Copy-Item -LiteralPath $Stage -Destination $work -Recurse -Force

# ★ 방금 지은 바이너리와 **그 구성의 런타임 DLL** 을 함께 덮는다.
#   exe 만 덮으면 Debug exe 가 Release DLL 을 찾다 0xC0000135 로 죽는다(실측).
Copy-Item -LiteralPath (Join-Path $binRoot 'Player.exe') -Destination $work -Force
Get-ChildItem -LiteralPath $binRoot -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $work -Force
}
$managedSource = Join-Path $repoRoot "Bin\x64-$Config\Managed"
if (Test-Path -LiteralPath $managedSource) {
    Copy-Item -Path (Join-Path $managedSource '*') -Destination (Join-Path $work 'Managed') -Recurse -Force
}

$failures = New-Object System.Collections.Generic.List[string]

function Stop-AllPlayers {
    Get-Process Player -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
}

Stop-AllPlayers
$exe = Join-Path $work 'Player.exe'
$proc = Start-Process -FilePath $exe -ArgumentList '--command-service' -WorkingDirectory $work `
    -RedirectStandardOutput (Join-Path $work 'player.out') `
    -RedirectStandardError  (Join-Path $work 'player.err') -PassThru

try {
    # endpoint 는 이 프로세스의 runtimeDataRoot 아래에 있다(Player 는 프로세스마다
    # 격리된 뿌리를 갖는다). pid 로 확인해 남의 것을 잡지 않는다.
    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    $info = $null
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) {
            $failures.Add("player-boot : Player 가 먼저 종료했다(exit=$($proc.ExitCode))")
            break
        }
        $found = @(Get-ChildItem -Path $env:TEMP -Recurse -Filter 'endpoint.json' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'CommandService' })
        foreach ($candidate in $found) {
            try {
                $parsed = Get-Content -LiteralPath $candidate.FullName -Raw | ConvertFrom-Json
                if ($parsed.pid -eq $proc.Id -and $parsed.port -gt 0) { $info = $parsed; break }
            } catch { }
        }
        if ($null -ne $info) { break }
        Start-Sleep -Milliseconds 400
    }
    if ($null -eq $info) {
        if ($failures.Count -eq 0) { $failures.Add('player-boot : 서비스가 뜨지 않았다') }
        throw '서비스에 붙지 못했다'
    }

    $base = "http://127.0.0.1:$($info.port)"
    $auth = @{ Authorization = "Bearer $($info.token)" }

    function Send([string]$Body) {
        $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
             -ContentType 'application/json' -Body $Body -UseBasicParsing `
             -SkipHttpErrorCheck -TimeoutSec 60
        return [pscustomobject]@{ Http = $r.StatusCode; Json = ($r.Content | ConvertFrom-Json) }
    }

    # ── ① role ──────────────────────────────────────────────────────────
    $healthRaw = Invoke-WebRequest ($base + '/health') -Headers $auth -UseBasicParsing -SkipHttpErrorCheck
    $health = $healthRaw.Content | ConvertFrom-Json
    "{0,-26} http={1} role={2}" -f 'health-role', $healthRaw.StatusCode, $health.role
    if ($health.role -ne 'player') {
        $failures.Add("health-role : role 이 player 가 아니다($($health.role)) — 붙은 것이 Player registry 가 맞는지 알 수 없다")
    }

    # ── ② Player role 을 가진 이름만 있는가 ─────────────────────────────
    $commandsRaw = Invoke-WebRequest ($base + '/commands') -Headers $auth -UseBasicParsing -SkipHttpErrorCheck
    $commands = $commandsRaw.Content | ConvertFrom-Json
    $names = @($commands.commands | ForEach-Object { $_.name } | Sort-Object)
    "{0,-26} count={1} names={2}" -f 'player-registry', $commands.count, ($names -join ',')

    $wrongRole = @($commands.commands | Where-Object { $_.roles -ne 'player' -and $_.roles -ne 'both' })
    if ($wrongRole.Count -gt 0) {
        $failures.Add("player-registry : roles 에 Player 가 없는 명령이 등록돼 있다: $(($wrongRole | ForEach-Object { $_.name }) -join ', ')")
    }
    $expectedNames = @('help','quit','player.status','player.scene','player.objects','player.object','player.move') | Sort-Object
    if (@(Compare-Object $expectedNames $names).Count -ne 0 -or $commands.count -ne $expectedNames.Count) {
        $failures.Add("Player discovery differs from the supported command contract: $($names -join ',')")
    }

    # ── ③ 상태를 읽는다 ────────────────────────────────────────────────
    $scene = Send '{"command":"player.scene","mode":"sync"}'
    "{0,-26} http={1} scene={2} objects={3}" -f 'read-scene', $scene.Http, $scene.Json.data.name, $scene.Json.data.objects
    if ($scene.Json.status -ne 'succeeded' -or $scene.Json.data.objects -lt 1) {
        $failures.Add("read-scene : 씬 상태를 읽지 못했다(status=$($scene.Json.status))")
    }

    $objects = Send '{"command":"player.objects","mode":"sync"}'
    if ($objects.Json.status -ne 'succeeded' -or $objects.Json.data.count -lt 1) {
        $failures.Add('read-objects : 오브젝트 목록이 비었다 — 아래 왕복이 무의미해진다')
        throw '오브젝트가 없다'
    }
    $target = $objects.Json.data.names[0]
    "{0,-26} count={1} target={2}" -f 'read-objects', $objects.Json.data.count, $target

    # ── ④ 런타임 명령이 재시작 없이 반영된다 ───────────────────────────
    #
    # 값을 실행마다 다르게 잡는다. 고정값을 쓰면 "이미 그 값이었다" 와
    # "옮겨서 그 값이 됐다" 가 구분되지 않는다.
    $rng = [Random]::new()
    $x = [math]::Round($rng.NextDouble() * 20 - 10, 3)
    $y = [math]::Round($rng.NextDouble() * 20 - 10, 3)
    $z = [math]::Round($rng.NextDouble() * 20 - 10, 3)

    $before = Send ('{"command":"player.object","args":["' + $target + '"],"mode":"sync"}')
    $moved  = Send ('{"command":"player.move","args":["' + $target + '","' + $x + '","' + $y + '","' + $z + '"],"mode":"sync"}')
    $after  = Send ('{"command":"player.object","args":["' + $target + '"],"mode":"sync"}')

    "{0,-26} before=({1},{2},{3}) after=({4},{5},{6}) 기대=({7},{8},{9})" -f 'live-mutation',
        $before.Json.data.position.x, $before.Json.data.position.y, $before.Json.data.position.z,
        $after.Json.data.position.x,  $after.Json.data.position.y,  $after.Json.data.position.z,
        $x, $y, $z

    if ($moved.Json.status -ne 'succeeded') {
        $failures.Add("live-mutation : player.move 가 실패했다(status=$($moved.Json.status) code=$($moved.Json.code))")
    }
    $tolerance = 0.001
    if ([math]::Abs($after.Json.data.position.x - $x) -gt $tolerance -or
        [math]::Abs($after.Json.data.position.y - $y) -gt $tolerance -or
        [math]::Abs($after.Json.data.position.z - $z) -gt $tolerance) {
        $failures.Add('live-mutation : 되읽은 값이 넣은 값과 다르다 — 명령이 실행 중인 게임에 반영되지 않았다(§11.3)')
    }

    # ── ⑤ Editor 전용 명령은 **부재**한다 ──────────────────────────────
    foreach ($editorOnly in @('scene.save', 'prefab.create', 'script.invoke', 'commands.selftest')) {
        $r = Send ('{"command":"' + $editorOnly + '","mode":"sync"}')
        "{0,-26} {1,-18} http={2} code={3}" -f 'editor-only-absent', $editorOnly, $r.Http, $r.Json.code
        if ($r.Http -ne 404 -or $r.Json.code -ne 'command.unknown') {
            $failures.Add(("editor-only-absent : $editorOnly 가 404 command.unknown 이 아니다" +
                "(http=$($r.Http) code=$($r.Json.code)) — 부재가 아니라 런타임 거부라면 §11.2 가 아니다"))
        }
    }

    # ── quit 이 정상 종료 경로를 탄다 ──────────────────────────────────
    $null = Send '{"command":"quit","mode":"sync"}'
    if (-not $proc.WaitForExit(30000)) {
        $failures.Add('quit : quit 뒤 30초 안에 종료하지 않았다')
    }
    else {
        "{0,-26} exit={1}" -f 'quit', $proc.ExitCode
        if ($proc.ExitCode -ne 0) {
            $failures.Add("quit : 정상 종료가 아니다(exit=$($proc.ExitCode))")
        }
    }

    # ── ⑥ 서비스를 쓰고도 authoring 텍스트 파서를 부르지 않았는가 (§11.2) ──
    #
    # ★ **스모크보다 강한 검사다.** 스모크는 서비스를 아예 켜지 않으므로, 서비스가
    #   authoring 문서 경로를 건드려도 스모크의 `calls=0` 은 그대로 0 이다. 위에서
    #   이 세션은 명령을 열 몇 번 태웠고 — JSON 을 파싱하고 씬을 읽고 오브젝트를
    #   옮겼다 — 그러고도 0 이어야 "Player 전용 JSON codec 이 authoring 경로를
    #   쓰지 않는다" 가 검증된다.
    #
    #   계수는 종료 경로에서 찍힌다(LC8 이 스모크 분기 밖으로 옮겼다). 위 quit 이
    #   정상 종료를 태웠으므로 이 시점에는 출력에 들어 있다.
    $playerOut = Get-Content -LiteralPath (Join-Path $work 'player.out') -Raw -ErrorAction SilentlyContinue
    $parser = [regex]::Match([string]$playerOut, '\[runtime\.text-parser\]\s*calls=(\d+)')
    if (-not $parser.Success) {
        $failures.Add('text-parser : 종료 출력에 [runtime.text-parser] 계수가 없다 — 계약이 관측되지 않았다')
    }
    else {
        $calls = [int]$parser.Groups[1].Value
        "{0,-26} calls={1}" -f 'text-parser', $calls
        if ($calls -ne 0) {
            $contexts = @([regex]::Matches([string]$playerOut, '\[runtime\.text-parser\.call\]\s*source=([^\r\n]+)') |
                ForEach-Object { $_.Groups[1].Value })
            $failures.Add("text-parser : 서비스 세션이 authoring 텍스트 파서를 $calls 번 불렀다: $($contexts -join ', ') — Player 전용 JSON codec 이 authoring 경로를 타고 있다(§11.2)")
        }
    }
}
finally {
    Stop-AllPlayers
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"Player 명령 서비스 통과 — 상태를 읽고, 런타임 명령이 재시작 없이 반영되고, Editor 명령은 부재한다"
exit 0
