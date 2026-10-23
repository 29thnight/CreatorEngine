[CmdletBinding()]
param(
    [ValidateSet('Editor', 'Player')][string]$Role = 'Editor',
    [string]$Stage = '',
    [ValidateRange(1, 8)][int]$Runs = 2
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo ('Build/Verification/CommandServiceShutdown/' + $Role + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
$results = @()

function Send-Bytes($Client, [string]$Text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Text)
    $Client.GetStream().Write($bytes, 0, $bytes.Length)
}

function Read-Response($Client) {
    $stream = $Client.GetStream()
    $header = ''
    while (!$header.EndsWith("`r`n`r`n")) {
        $next = $stream.ReadByte()
        if ($next -lt 0 -or $header.Length -ge 8192) { throw 'Incomplete HTTP response headers' }
        $header += [char]$next
    }
    $length = [regex]::Match($header, '(?i)Content-Length: (\d+)')
    if (!$length.Success) { throw 'Missing response length' }
    $body = New-Object byte[] ([int]$length.Groups[1].Value)
    $offset = 0
    while ($offset -lt $body.Length) {
        $count = $stream.Read($body, $offset, $body.Length - $offset)
        if ($count -le 0) { throw 'Incomplete HTTP response body' }
        $offset += $count
    }
    return @{ status = [int]$header.Split(' ')[1]; body = [Text.Encoding]::UTF8.GetString($body) }
}

function Connect-Client([int]$Port, $Clients) {
    $client = [Net.Sockets.TcpClient]::new('127.0.0.1', $Port)
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $Clients.Add($client)
    return $client
}

function Input-Identity([string]$Directory) {
    return @(Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Directory.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash
    }) -join "`n"
}

$exe = Join-Path $repo 'Bin/x64-Release/Editor/CreatorEditor.exe'
$playerStage = ''
if ($Role -eq 'Player') {
    if (!(Test-Path -LiteralPath (Join-Path $Stage 'GameAssets.pak'))) { throw '-Stage requires an accepted cooked Player package' }
    $playerStage = Join-Path $output 'PlayerStage'
    Copy-Item -LiteralPath $Stage -Destination $playerStage -Recurse
    foreach ($name in @('Player.exe', 'Player.runtime.dll')) {
        Copy-Item -LiteralPath (Join-Path $repo "Bin/x64-Release/Player/$name") -Destination $playerStage -Force
    }
    $exe = Join-Path $playerStage 'Player.exe'
    $playerIdentity = Input-Identity $playerStage
} elseif (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw 'This gate needs its own Editor session; an Editor is already running'
}

for ($run = 1; $run -le $Runs; ++$run) {
    $work = Join-Path $output "Run-$run"
    $runtimeTemp = Join-Path $work 'Temp'
    New-Item -ItemType Directory -Force -Path $runtimeTemp | Out-Null
    $arguments = @('--command-service')
    $directory = $playerStage
    if ($Role -eq 'Editor') {
        $project = Join-Path $work 'Project'
        New-Item -ItemType Directory -Force -Path "$project/Assets/Scenes", "$project/Assets/Script" | Out-Null
        Copy-Item -LiteralPath "$repo/Dynamic_CPP/ProjectSetting" -Destination $project -Recurse
        Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" -Destination "$project/Assets/Shaders" -Recurse
        Set-Content -LiteralPath "$project/Assets/Script/compile.order" -Value '' -Encoding utf8
        $arguments += @('--development-project', ('"' + $project + '"'), '--smoke-offscreen')
        $directory = Split-Path $exe
    }

    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    try {
        # This script process owns the environment override. Only its newly
        # started child inherits it; no system/user environment is modified.
        $env:TEMP = $runtimeTemp
        $env:TMP = $runtimeTemp
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $directory -WindowStyle Hidden `
            -RedirectStandardOutput "$work/stdout.log" -RedirectStandardError "$work/stderr.log" -PassThru
        $null = $process.Handle
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
    }

    $clients = [Collections.Generic.List[Net.Sockets.TcpClient]]::new()
    try {
        $endpoint = $null
        $endpointPath = ''
        $deadline = [DateTime]::UtcNow.AddSeconds(120)
        do {
            if ($process.HasExited) { throw "Host exited before endpoint: $($process.ExitCode)" }
            $candidates = if ($Role -eq 'Editor') { @("$project/Library/CommandService/endpoint.json") } else {
                @(Get-ChildItem -LiteralPath $runtimeTemp -Recurse -Filter endpoint.json -File | ForEach-Object FullName)
            }
            foreach ($candidate in $candidates) {
                if (Test-Path -LiteralPath $candidate) {
                    try {
                        $info = Get-Content -LiteralPath $candidate -Raw | ConvertFrom-Json
                        if ($info.pid -eq $process.Id) { $endpoint = $info; $endpointPath = $candidate; break }
                    } catch {}
                }
            }
            if (!$endpoint) { Start-Sleep -Milliseconds 100 }
        } until ($endpoint -or [DateTime]::UtcNow -ge $deadline)
        if (!$endpoint) { throw 'Owned command service endpoint not found' }

        # Keep every client alive until the host has exited. This deliberately
        # reproduces pooled HTTP/1.1 connections independently of shell behavior.
        for ($connection = 0; $connection -lt 3; ++$connection) {
            $client = Connect-Client $endpoint.port $clients
            Send-Bytes $client ("GET /health HTTP/1.1`r`nHost: 127.0.0.1`r`nAuthorization: Bearer " + $endpoint.token + "`r`nConnection: keep-alive`r`n`r`n")
            $response = Read-Response $client
            $health = $response.body | ConvertFrom-Json
            if ($response.status -ne 200 -or $health.role -ne $Role.ToLowerInvariant()) { throw 'Wrong service role/health response' }
        }
        $idle = Connect-Client $endpoint.port $clients
        $partial = Connect-Client $endpoint.port $clients
        Send-Bytes $partial "GET /health HTTP/1.1`r`nHost: 127.0.0.1`r`n"

        $base = 'http://127.0.0.1:' + $endpoint.port
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $null = Invoke-RestMethod "$base/command" -Method Post -Headers @{ Authorization = 'Bearer ' + $endpoint.token } `
            -ContentType application/json -Body '{"command":"quit","args":[],"mode":"async"}' -TimeoutSec 5
        if (!$process.WaitForExit(5000)) { throw 'Host shutdown exceeded 5 seconds with live HTTP clients' }
        $watch.Stop()
        if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) { throw "Unexpected host exit: $($process.ExitCode)" }
        if (Test-Path -LiteralPath $endpointPath) { throw 'Endpoint survived normal shutdown' }
        if ($Role -eq 'Editor') {
            $traces = @(Get-ChildItem -LiteralPath $work -Filter shutdown_trace.txt -Recurse -File)
            if (!$traces.Count -or !(@($traces | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -match '\[9\]')) {
                throw 'Final native Editor shutdown trace missing'
            }
        } else {
            # Player has its own bootstrap rather than Editor's [9] trace.
            # Verify its actual presentation/render consumers drained instead.
            $stdout = [IO.File]::ReadAllText("$work/stdout.log")
            if ($stdout -notmatch '\[PresentationThread\] shutdown[^\r\n]*pending 0[^\r\n]*balanced 1' -or
                $stdout -notmatch '\[RenderThread\] shutdown drain[^\r\n]*pending 0[^\r\n]*balanced 1') {
                throw 'Player native render consumers did not drain'
            }
        }
        if ($Role -eq 'Player' -and (Input-Identity $playerStage) -ne $playerIdentity) { throw 'Player mutated cooked package inputs' }
        $results += @{ run = $run; pid = $process.Id; elapsedMs = $watch.Elapsed.TotalMilliseconds; exitCode = $process.ExitCode;
            keepAliveClients = 3; idleClients = 1; partialClients = 1; endpointRemoved = $true; nativeFinalization = $true }
    } finally {
        foreach ($client in $clients) { $client.Dispose() }
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

@{ result = 'COMMAND_SERVICE_SHUTDOWN_PRODUCT_OK'; role = $Role; shell = $PSVersionTable.PSVersion.ToString(); runs = $results;
   hostHash = (Get-FileHash -LiteralPath ($exe.Replace('.exe', '.runtime.dll'))).Hash; packageImmutable = ($Role -eq 'Player') } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$output/result.json" -Encoding utf8
"COMMAND_SERVICE_SHUTDOWN_PRODUCT_OK role=$Role shell=$($PSVersionTable.PSVersion) runs=$Runs evidence=$output"
