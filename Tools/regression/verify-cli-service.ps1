[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc4"),

    [ValidateRange(5, 600)]
    [int]$BootTimeoutSec = 120
)

# LC4 (PHASE 14.5) — 로컬 HTTP/JSON 서비스 게이트.
#
# ── 이것은 실행 표면이다 (§8) ───────────────────────────────────────────
#
# HTTP 로 열리는 순간 `MutatesAssets` 명령이 프로세스 경계 밖에서 호출 가능해진다.
# 그래서 이 게이트는 "동작하는가"보다 **"열리지 말아야 할 것이 닫혀 있는가"**를
# 먼저 본다. 기능 검사만 있고 통제 검사가 없으면, 통제가 조용히 사라져도 초록이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'
$failures = New-Object System.Collections.Generic.List[string]

if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }
$script:gateProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
function Stop-Editors {
    foreach ($owned in $script:gateProcesses) {
        try { if (-not $owned.HasExited) { $owned.Kill(); $owned.WaitForExit(15000) | Out-Null } } catch { }
    }
    $script:gateProcesses.Clear()
}

# ★ 파이프가 아니라 파일로 리다이렉트한다.
#
#   처음에는 파이프로 받고 읽지 않았다. 에디터는 부팅·종료에 많이 찍으므로
#   파이프 버퍼가 차면 **쓰기에서 막힌다** — 그 상태에서 quit 을 보내면 종료
#   절차가 끝까지 못 가고, endpoint 파일이 남은 채 하네스가 프로세스를 죽인다.
#   실제로 그렇게 실패했고, 증상은 "정상 종료 뒤에도 파일이 남았다"였다.
function Start-Editor([string[]]$Arguments) {
    $started = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $exeDir -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Work 'service.out') `
        -RedirectStandardError  (Join-Path $Work 'service.err') -PassThru
    $script:gateProcesses.Add($started)
    return $started
}

# ★ 파일이 "있다"로 기다리면 안 된다.
#
#   유령 endpoint 검사가 미리 파일을 만들어 두므로, 존재만 보면 **유령을 읽고**
#   포트 1 로 접속하려다 연결 거부로 끝난다(실제로 그렇게 실패했다). 우리가
#   띄운 프로세스의 pid 가 적힐 때까지 기다린다.
function Wait-Endpoint([int]$Seconds, [int]$ExpectPid) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $endpointPath) {
            try {
                $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
                if ($parsed.pid -eq $ExpectPid -and $parsed.port -gt 0) { return $parsed }
            } catch { }
        }
        Start-Sleep -Milliseconds 300
    }
    return $null
}

Stop-Editors
Remove-Item -LiteralPath $endpointPath -ErrorAction SilentlyContinue

# ── 1) 서비스는 기본 off 다 ─────────────────────────────────────────────
#
# 켜는 것은 명시 플래그뿐이다. 이 검사가 없으면 "언젠가 기본이 on 이 되는" 변경이
# 조용히 지나간다 — 실행 표면에서 가장 나쁜 회귀다.
$offScript = Join-Path $Work 'off.txt'
"help`nquit" | Set-Content -LiteralPath $offScript -Encoding UTF8
$off = Start-Process -FilePath $Exe -ArgumentList '--script', $offScript -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work 'off.out') `
    -RedirectStandardError  (Join-Path $Work 'off.err') -PassThru
$off.WaitForExit(180000) | Out-Null
if (-not $off.HasExited) { $off.Kill() }

if (Test-Path -LiteralPath $endpointPath) {
    $failures.Add('default-off : --command-service 없이 endpoint 파일이 생겼다')
    "default-off              FAIL — endpoint 파일이 생겼다"
}
else { "default-off              endpoint 파일 없음" }

# ── 2) 유령 endpoint 회수 ───────────────────────────────────────────────
#
# 크래시 뒤 남은 파일이 살아 있으면, 포트가 재사용될 때 클라이언트가 **다른
# 프로세스**에 명령을 보낸다. 죽은 pid 를 적어 두고 회수되는지 본다.
$ghost = @{ schemaVersion = 1; pid = 999999; port = 1; token = 'ghost'
            host = '127.0.0.1'; project = 'x'; role = 'editor'; startedUtc = 'x' }
New-Item -ItemType Directory -Force -Path (Split-Path $endpointPath -Parent) | Out-Null
$ghost | ConvertTo-Json | Set-Content -LiteralPath $endpointPath -Encoding UTF8

$proc = Start-Editor @('--command-service', '--console')
$info = Wait-Endpoint $BootTimeoutSec $proc.Id
if ($null -eq $info) {
    Stop-Editors
    $failures.Add('ghost-reclaim/boot : 유령 endpoint 를 회수하고 새로 쓰지 못했다')
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"ghost-reclaim            죽은 pid(999999)를 회수하고 새로 썼다 (pid=$($info.pid) port=$($info.port))"

$base = "http://127.0.0.1:$($info.port)"
$auth = @{ Authorization = "Bearer $($info.token)" }

function Req($path, $headers, $method = 'GET', $body = $null, $contentType = 'application/json') {
    $args = @{ Uri = "$base$path"; Method = $method; UseBasicParsing = $true; SkipHttpErrorCheck = $true }
    if ($null -ne $headers) { $args.Headers = $headers }
    if ($null -ne $body) { $args.Body = $body; $args.ContentType = $contentType }
    return Invoke-WebRequest @args
}

try {
    # ── 3) §8 통제 ──────────────────────────────────────────────────────
    #
    # `/health` 도 예외가 아니다. 토큰 없이 답하는 경로가 하나라도 생기면
    # 웹페이지가 로컬 에디터를 조작할 수 있다.
    $checks = @(
        @{ label = 'auth-none';    expect = 401; got = (Req '/health' $null).StatusCode }
        @{ label = 'auth-wrong';   expect = 401; got = (Req '/health' @{Authorization='Bearer nope'}).StatusCode }
        @{ label = 'auth-ok';      expect = 200; got = (Req '/health' $auth).StatusCode }
        @{ label = 'origin-block'; expect = 403; got = (Req '/health' ($auth + @{Origin='http://evil.example'})).StatusCode }
        @{ label = 'referer-block';expect = 403; got = (Req '/health' ($auth + @{Referer='http://evil.example'})).StatusCode }
        @{ label = 'route-unknown';expect = 404; got = (Req '/nope' $auth).StatusCode }
    )
    foreach ($c in $checks) {
        "{0,-24} HTTP {1} (기대 {2})" -f $c.label, $c.got, $c.expect
        if ($c.got -ne $c.expect) { $failures.Add("$($c.label) : HTTP $($c.got) ≠ $($c.expect)") }
    }

    # 본문 상한. 1MiB 를 넘기면 413 이고 프로세스는 살아 있어야 한다.
    $big = '{"command":"help","args":["' + ('x' * (1024 * 1024 + 64)) + '"]}'
    $r = Req '/command' $auth 'POST' $big
    "{0,-24} HTTP {1} (기대 413)" -f 'body-too-large', $r.StatusCode
    if ($r.StatusCode -ne 413) { $failures.Add("body-too-large : HTTP $($r.StatusCode) ≠ 413") }

    # ★ 헤더 개수 상한. 검토가 "지워도 초록"이라고 지적한 자리다.
    $many = @{}
    $many['Authorization'] = "Bearer $($info.token)"
    1..80 | ForEach-Object { $many["X-Pad-$_"] = 'v' }
    $r = Req '/health' $many
    "{0,-24} HTTP {1} (기대 413)" -f 'header-count', $r.StatusCode
    if ($r.StatusCode -ne 413) { $failures.Add("header-count : HTTP $($r.StatusCode) ≠ 413 — 헤더 64개 상한이 없다") }

    # ★ chunked 거부. 요청 밀수 방어 전체가 이 한 줄에 걸려 있는데 검사가 없었다.
    #   Invoke-WebRequest 로는 Transfer-Encoding 을 못 붙이므로 raw 소켓으로 보낸다.
    $chunkStatus = 'none'
    try {
        $tcp = [Net.Sockets.TcpClient]::new('127.0.0.1', $info.port)
        $stream = $tcp.GetStream()
        $req = "POST /command HTTP/1.1`r`nHost: 127.0.0.1`r`nAuthorization: Bearer $($info.token)`r`n" +
               "Content-Type: application/json`r`nTransfer-Encoding: chunked`r`n`r`n0`r`n`r`n"
        $bytes = [Text.Encoding]::ASCII.GetBytes($req)
        $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
        $buf = New-Object byte[] 256
        $tcp.ReceiveTimeout = 8000
        $n = $stream.Read($buf, 0, $buf.Length)
        $chunkStatus = ([Text.Encoding]::ASCII.GetString($buf, 0, $n) -split "`r`n")[0]
        $tcp.Close()
    } catch { $chunkStatus = "예외: $($_.Exception.Message)" }
    "{0,-24} {1} (기대 501)" -f 'chunked-reject', $chunkStatus
    if ($chunkStatus -notmatch '501') {
        $failures.Add("chunked-reject : Transfer-Encoding 이 거부되지 않았다 — $chunkStatus")
    }

    # ★ 요청 절대 기한. 아무것도 안 보내는 연결이 서비스를 잡지 못해야 한다.
    #   같은 시각에 정상 요청이 답을 받는지로 판정한다 — 이것이 없으면 인증
    #   이전 단계의 서비스 정지가 회귀해도 초록이다.
    $slow = $null
    try {
        $slow = [Net.Sockets.TcpClient]::new('127.0.0.1', $info.port)
        $slowStream = $slow.GetStream()
        $head = [Text.Encoding]::ASCII.GetBytes("GET /health HTTP/1.1`r`n")   # 미완성 요청
        $slowStream.Write($head, 0, $head.Length); $slowStream.Flush()

        $sw = [Diagnostics.Stopwatch]::StartNew()
        $r = Req '/health' $auth
        $sw.Stop()
        "{0,-24} HTTP {1} · 정상 요청 {2:F0}ms (느린 연결이 붙어 있는 동안)" -f 'slowloris', $r.StatusCode, $sw.Elapsed.TotalMilliseconds
        if ($r.StatusCode -ne 200) {
            $failures.Add("slowloris : 느린 연결이 붙어 있는 동안 정상 요청이 $($r.StatusCode) 로 실패했다")
        }
        if ($sw.Elapsed.TotalSeconds -gt 5) {
            $failures.Add("slowloris : 정상 요청이 $([int]$sw.Elapsed.TotalSeconds)초 걸렸다 — 수신 루프가 직렬화돼 있다")
        }
    }
    catch { $failures.Add("slowloris : $($_.Exception.Message)") }
    finally { if ($null -ne $slow) { $slow.Close() } }

    # ── 4) 명령이 값으로 돌아온다 ───────────────────────────────────────
    #
    # ★ 예전에는 이 검사가 `wait 5` 로 `data.frames == 5` 를 봤다. LC5 가
    #   **서비스 세션에서 `wait` 를 금지**하면서 그 표본을 쓸 수 없게 됐다 —
    #   전역 프레임 보류는 자기 요청만 늦추는 것이 아니라 다른 요청 전부의
    #   지연이 되기 때문이다(§7.2). 표본을 `commands.describe` 로 바꾼다:
    #   값을 돌려주고, 프레임을 잡지 않고, 결정적이다.
    #
    #   금지 자체는 아래에서 따로 단정한다. 검사를 조용히 바꾸기만 하면
    #   "왜 이 표본이 바뀌었나"가 기록에서 사라진다.
    $r = Req '/command' $auth 'POST' '{"command":"commands.describe","args":["help"],"correlationId":"lc4"}'
    $json = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} status={2} data.canonical={3}" -f 'command-ok', $r.StatusCode, $json.status, $json.data.canonical
    if ($r.StatusCode -ne 200)             { $failures.Add("command-ok : HTTP $($r.StatusCode)") }
    if ($json.status -ne 'succeeded')      { $failures.Add("command-ok : status=$($json.status)") }
    if ($json.data.canonical -ne 'help')   { $failures.Add('command-ok : data 가 값으로 돌아오지 않았다') }
    if ($json.correlationId -ne 'lc4')     { $failures.Add('command-ok : correlationId 가 되돌아오지 않았다') }
    if ($null -eq $json.timing.queuedMs)   { $failures.Add('command-ok : timing 이 없다') }

    # LC5 가 들인 금지를 여기서도 못 박는다. 이 줄이 붉어지면 서비스가 전역
    # 프레임 보류를 다시 받아들이기 시작했다는 뜻이다.
    $r = Req '/command' $auth 'POST' '{"command":"wait","args":["5"]}'
    $json = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} code={2} (LC5 이후 금지)" -f 'wait-forbidden', $r.StatusCode, $json.code
    if ($r.StatusCode -ne 400 -or $json.code -ne 'service.wait_forbidden') {
        $failures.Add("wait-forbidden : 서비스가 wait 를 받았다 (HTTP $($r.StatusCode))")
    }

    # ── 5) 상태 사상 (§5.3) ─────────────────────────────────────────────
    #
    # ★ `precondition` 은 **`mode:"sync"` 를 명시한다.**
    #
    #   `scene.load` 는 `cost=Long` 이라, LC5 이후 기본 `auto` 에서는 실행하기도
    #   전에 202 + operationId 로 승격된다 — 그러면 여기서 보려는 동기 상태
    #   사상(§5.3)이 아예 일어나지 않는다. 승격 자체는 옳은 거동이므로 게이트가
    #   기대값을 202 로 낮추는 것이 아니라, **동기 경로를 골라서** 사상을 본다.
    #   기대값을 옮겼다면 "409 를 내야 할 실패가 409 로 온다"는 검사가 조용히
    #   사라졌을 것이다.
    $map = @(
        @{ label='precondition'; expect=409; body='{"command":"scene.load","args":["Assets/Scenes/NoSuchScene.creator"],"mode":"sync"}' }
        @{ label='bad-args';     expect=400; body='{"command":"wait","args":["abc"]}' }
        @{ label='unknown-cmd';  expect=404; body='{"command":"this.does.not.exist"}' }
        @{ label='args-type';    expect=400; body='{"command":"wait","args":[7]}' }
        @{ label='bad-json';     expect=400; body='{"command":' }
    )
    foreach ($m in $map) {
        $r = Req '/command' $auth 'POST' $m.body
        "{0,-24} HTTP {1} (기대 {2})" -f $m.label, $r.StatusCode, $m.expect
        if ($r.StatusCode -ne $m.expect) { $failures.Add("$($m.label) : HTTP $($r.StatusCode) ≠ $($m.expect)") }
    }

    # data 는 값이 없어도 객체다(§5.2).
    $r = Req '/command' $auth 'POST' '{"command":"help"}'
    $json = $r.Content | ConvertFrom-Json
    if ($null -eq $json.data) { $failures.Add('data-shape : 값이 없을 때 data 가 null 이다(객체여야 한다)') }
    else { "{0,-24} 값이 없어도 객체" -f 'data-shape' }

    # ── 6) 구조화 인자가 라인 문법을 거치지 않는다 ──────────────────────
    #
    # LC2 가 연 경로다. JSON 이 갈라 온 값을 이어 붙였다 다시 자르면 §3.2 의
    # 왕복 손실이 되살아난다 — 공백이 든 이름 둘로 그것을 확인한다.
    $r = Req '/command' $auth 'POST' '{"command":"cli.echo.args","args":["Big Boss Character","Main Characters"]}'
    $stdout = ''   # 출력은 에디터 stdout 으로 가므로 여기서는 상태만 본다
    "{0,-24} HTTP {1}" -f 'structured-args', $r.StatusCode
    if ($r.StatusCode -ne 200) { $failures.Add("structured-args : HTTP $($r.StatusCode)") }

    # ── 7) discovery 가 LC3 snapshot 을 낸다 ────────────────────────────
    $r = Req '/commands' $auth
    $json = $r.Content | ConvertFrom-Json
    "{0,-24} HTTP {1} count={2}" -f 'discovery', $r.StatusCode, $json.count
    if ($r.StatusCode -ne 200)       { $failures.Add("discovery : HTTP $($r.StatusCode)") }
    if ($json.count -ne @($json.commands).Count) { $failures.Add('discovery : count disagrees with entries') }
    foreach ($name in @('scene.load','object.create','object.describe','object.rename','object.delete','undo')) {
        if ($json.commands.name -notcontains $name) { $failures.Add("discovery : product command missing: $name") }
    }
    foreach ($name in @('selftest','dx12.selftest','scene.hierarchymutation','experiment.matparity')) {
        if ($json.commands.name -contains $name) { $failures.Add("discovery : harness leaked into product commands: $name") }
    }

    $r = Req '/commands/scene.load' $auth
    $one = $r.Content | ConvertFrom-Json
    if ($one.cost -ne 'long' -or $one.aliases -notcontains 'scene.switch') {
        $failures.Add('discovery : descriptor 상세가 LC3 값과 다르다')
    }
    $r = Req '/commands/nope' $auth
    if ($r.StatusCode -ne 404) { $failures.Add("discovery : 없는 명령이 404 가 아니다($($r.StatusCode))") }

    # ── 7.5) endpoint 파일에 토큰이 있다 — 권한이 실제로 제한돼야 한다 ──
    #
    # ★ 이 검사가 없어서 CRITICAL 하나가 통째로 지나갈 뻔했다.
    #
    #   처음 구현은 `SetFileAttributesW(FILE_ATTRIBUTE_NORMAL)` 을 부르고
    #   주석에 "상속을 끊고 소유자 권한만 남긴다"고 적어 뒀다. 그 함수는 속성
    #   비트만 건드리고 ACL 은 손도 안 댄다 — 평문 토큰이 부모 디렉터리에서
    #   상속한 권한 그대로 놓여 있었다. 코드와 주석을 읽는 것만으로는 못 잡고,
    #   **실제 DACL 을 보는 검사**만 잡는다.
    $acl = Get-Acl -LiteralPath $endpointPath
    $me  = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $allowed = @($acl.Access | Where-Object { $_.AccessControlType -eq 'Allow' })
    $foreign = @($allowed | Where-Object {
        $sid = try { $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value }
               catch { $_.IdentityReference.Value }
        # 자기 자신·SYSTEM·Administrators 외에 접근권이 있으면 토큰이 샌다.
        $sid -ne $me -and $sid -ne 'S-1-5-18' -and $sid -ne 'S-1-5-32-544'
    })
    "{0,-24} 상속={1} · 허용 항목 {2}개 · 외부 SID {3}개" -f `
        'endpoint-acl', (-not $acl.AreAccessRulesProtected -eq $false), $allowed.Count, $foreign.Count
    if (-not $acl.AreAccessRulesProtected) {
        $failures.Add('endpoint-acl : DACL 상속이 끊기지 않았다 — 부모 디렉터리 권한이 토큰 파일에 붙는다')
    }
    if ($foreign.Count -gt 0) {
        $names = ($foreign | ForEach-Object { $_.IdentityReference.Value }) -join ', '
        $failures.Add("endpoint-acl : 현재 사용자 외에 접근권이 있다 — $names")
    }

    # ── 8) 감사 로그가 남는다 (토큰은 안 남는다) ────────────────────────
    $auditPath = Join-Path (Split-Path $endpointPath -Parent) 'audit.log'
    if (-not (Test-Path -LiteralPath $auditPath)) { $failures.Add('audit : 감사 로그가 없다') }
    else {
        $audit = Get-Content -LiteralPath $auditPath -Raw
        "{0,-24} {1} 줄" -f 'audit', (($audit -split "`n").Count - 1)
        if ($audit.Contains($info.token)) { $failures.Add('audit : 감사 로그에 토큰이 남았다') }
    }
}
finally {
    # 정상 종료로 endpoint 파일이 회수되는지 본다.
    $r = $null
    try { $r = Req '/command' $auth 'POST' '{"command":"quit"}' } catch { }
    if ($null -ne $proc) { $proc.WaitForExit(60000) | Out-Null; if (-not $proc.HasExited) { $proc.Kill() } }
    Stop-Editors
}

Start-Sleep -Milliseconds 500
if (Test-Path -LiteralPath $endpointPath) {
    $failures.Add('endpoint-cleanup : 정상 종료 뒤에도 endpoint 파일이 남았다')
}
else { "endpoint-cleanup         정상 종료 뒤 파일 삭제됨" }

# ── 9) 정적 게이트 ──────────────────────────────────────────────────────
#
# §14.1 — 비 loopback bind 0, winsock 헤더가 이음매 밖으로 새지 않는다.
$serviceDir = Join-Path $repoRoot 'Engine\CommandService'
$seamFile   = 'SocketPlatform_Win32.cpp'

$bindHits = @()
$winsockHits = @()
foreach ($file in (Get-ChildItem -LiteralPath $serviceDir -Recurse -File -Include *.h, *.cpp)) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if ($file.Name -ne $seamFile) {
        if ($text -match '#\s*include\s*<(winsock2|ws2tcpip)\.h>') { $winsockHits += $file.Name }
    }
    foreach ($line in ($text -split "`n")) {
        if ($line -match '^\s*(//|\*)') { continue }
        if ($line -match 'INADDR_ANY|0\.0\.0\.0|in6addr_any') { $bindHits += "$($file.Name): $($line.Trim())" }

        # ★ 이름만 막으면 뚫린다.
        #
        #   `INADDR_ANY` 는 그냥 0 이다. `s_addr = 0` 으로 쓰면 위 정규식은
        #   조용히 통과하고 서비스는 **모든 인터페이스에** 열린다. 그래서
        #   주소 대입 자체를 보고, INADDR_LOOPBACK 이 아닌 것을 전부 잡는다.
        if ($line -match 's_addr\s*=' -and $line -notmatch 'INADDR_LOOPBACK') {
            $bindHits += "$($file.Name): $($line.Trim())"
        }
    }
}
"{0,-24} winsock 헤더 이음매 밖 {1}건 · 비 loopback bind {2}건" -f 'static-gate', $winsockHits.Count, $bindHits.Count
if ($winsockHits.Count -gt 0) {
    $failures.Add("static-gate : winsock 헤더가 이음매 밖에 있다 — $($winsockHits -join ', ')")
}
if ($bindHits.Count -gt 0) {
    $failures.Add("static-gate : 비 loopback bind 가 있다 — $($bindHits -join ' | ')")
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"command service 전체 통과"
exit 0
