param(
    [Parameter(Mandatory)][string]$EditorExecutable,
    [Parameter(Mandatory)][string]$ProjectPath,
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $EditorExecutable).Path
$project = (Resolve-Path -LiteralPath $ProjectPath).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw 'Use a new evidence directory.' }
if ($TimeoutSeconds -le 0) { throw 'TimeoutSeconds must be positive.' }
New-Item -ItemType Directory -Path $out | Out-Null
$scenario = Join-Path $out 'scenario.txt'
@('dx12.ssgi', 'dx12.validation') | Set-Content -LiteralPath $scenario -Encoding utf8
$savedRuntime = $env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY
$savedValidation = $env:CREATOR_DX12_VALIDATION
$process = $null
$result = [ordered]@{
    passed = $false
    scope = 'DX12 per-view temporal lifetime and procedural SSGI pixels: abort/reset admission, two-view isolation, cut/resize and recreation.'
    executable = $exe
    binarySha256 = (Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash
    processStopped = $false
    cases = @()
}
try {
    $env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY = $runtime
    $env:CREATOR_DX12_VALIDATION = 'basic'
    $arguments = @('--development-project', $project, '--smoke-offscreen',
        '--commandlet-script', $scenario, '--result-format', 'jsonl',
        '--result-file', (Join-Path $out 'commands.jsonl'))
    # Start-Process joins the argument array; quote every path/token for Windows.
    if (@($arguments | Where-Object { $_.Contains('"') }).Count) { throw 'Arguments containing quotes are unsupported.' }
    $quoted = @($arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory (Split-Path $exe) `
        -WindowStyle Hidden -RedirectStandardOutput (Join-Path $out 'stdout.log') `
        -RedirectStandardError (Join-Path $out 'stderr.log') -PassThru
    $result.pid = $process.Id
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)") {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'SSGI commandlet timed out.' }
        Start-Sleep -Milliseconds 250
    }
    $process.Refresh()
    $result.exitCode = $process.ExitCode
    if ($process.ExitCode -ne 0) { throw "Editor failed with exit code $($process.ExitCode)." }
    $records = @(Get-Content -LiteralPath (Join-Path $out 'commands.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $ssgi = @($records | Where-Object command -eq 'dx12.ssgi')
    $validation = @($records | Where-Object command -eq 'dx12.validation')
    if ($ssgi.Count -ne 1 -or $ssgi[0].status -ne 'succeeded' -or $ssgi[0].data.passed -isnot [bool] -or !$ssgi[0].data.passed) {
        throw 'Missing or failed SSGI result.'
    }
    $matches = [regex]::Matches($ssgi[0].data.log,
        'SSGI abort recovery stage=(\d+) samples=(\d+) maxError=([0-9.eE+\-]+) passed=(\d+)')
    if ($matches.Count -ne 4) { throw 'Expected four GPU recovery comparisons.' }
    for ($stage = 0; $stage -lt 4; ++$stage) {
        $m = $matches[$stage]
        $errorValue = [double]::Parse($m.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ([int]$m.Groups[1].Value -ne $stage -or [int]$m.Groups[2].Value -ne 65536 -or
            [int]$m.Groups[4].Value -ne 1 -or ![double]::IsFinite($errorValue) -or $errorValue -gt 0.000001) {
            throw "Invalid GPU comparison for stage $stage."
        }
        $result.cases += @{ stage = $stage; samples = 65536; maxError = $errorValue }
    }
    if ($ssgi[0].data.log -notmatch 'TR3 view history checks=21 passed=1') { throw 'TR3 view history checks missing.' }
    $viewMatches = [regex]::Matches($ssgi[0].data.log, 'TR3 multiview reset=(\d+) view=(\d+) samples=(\d+) maxError=([0-9.eE+\-]+) passed=(\d+)')
    if ($viewMatches.Count -ne 4) { throw 'TR3 multiview pixel checks missing.' }
    $result.multiview = @()
    for ($i = 0; $i -lt 4; ++$i) {
        $m = $viewMatches[$i]
        $errorValue = [double]::Parse($m.Groups[4].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ([int]$m.Groups[1].Value -ne [int][Math]::Floor($i / 2) -or [int]$m.Groups[2].Value -ne ($i % 2) -or [int]$m.Groups[3].Value -ne 65536 -or
            ![double]::IsFinite($errorValue) -or $errorValue -gt 0.000001 -or [int]$m.Groups[5].Value -ne 1) {
            throw 'TR3 multiview pixel comparison failed.'
        }
        $result.multiview += @{ resetOther = ($i -ge 2); view = ($i % 2); samples = 65536; maxError = $errorValue }
    }
    $resetMatches = [regex]::Matches($ssgi[0].data.log,
        'SSGI reset recovery case=([a-z-]+) frame=(\d+) extent=(\d+)x(\d+) samples=(\d+) maxError=([0-9.eE+\-]+) surfaces=(\d+) accumulation=([0-9.eE+\-]+) passed=(\d+)')
    if ($resetMatches.Count -ne 8) { throw 'Expected eight reset/recreation GPU comparisons.' }
    $expectedCases = @('camera-cut', 'resize-down', 'resize-up', 'extent-only')
    $expectedWidths = @(128, 64, 192, 96)
    $expectedHeights = @(128, 96, 160, 64)
    $result.resetCases = @()
    for ($i = 0; $i -lt 8; ++$i) {
        $m = $resetMatches[$i]
        $caseIndex = [int][Math]::Floor($i / 2)
        $frame = $(if ($i % 2 -eq 0) { 1 } else { 8 })
        $errorValue = [double]::Parse($m.Groups[6].Value, [Globalization.CultureInfo]::InvariantCulture)
        $accumulation = [double]::Parse($m.Groups[8].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ($m.Groups[1].Value -ne $expectedCases[$caseIndex] -or [int]$m.Groups[2].Value -ne $frame -or
            [int]$m.Groups[3].Value -ne $expectedWidths[$caseIndex] -or [int]$m.Groups[4].Value -ne $expectedHeights[$caseIndex] -or
            [int]$m.Groups[5].Value -ne ($expectedWidths[$caseIndex] * $expectedHeights[$caseIndex] * 4) -or
            ![double]::IsFinite($errorValue) -or $errorValue -gt 0.000001 -or [int]$m.Groups[7].Value -le 0 -or
            ![double]::IsFinite($accumulation) -or ($frame -eq 1 -and $accumulation -ne 1) -or
            ($frame -eq 8 -and $accumulation -lt 4) -or [int]$m.Groups[9].Value -ne 1) {
            throw "Invalid reset/recreation GPU comparison $i."
        }
        $result.resetCases += @{ case = $m.Groups[1].Value; frame = $frame; width = $expectedWidths[$caseIndex];
            height = $expectedHeights[$caseIndex]; samples = [int]$m.Groups[5].Value; maxError = $errorValue;
            surfaces = [int]$m.Groups[7].Value; accumulation = $accumulation }
    }
    if ($validation.Count -ne 1 -or $validation[0].status -ne 'succeeded' -or
        $validation[0].data.layerEnabled -isnot [bool] -or !$validation[0].data.layerEnabled -or
        $validation[0].data.mode -ne 'basic' -or $validation[0].data.problems -ne 0 -or
        $validation[0].data.messages -ne 0 -or $validation[0].data.droppedMessages -ne 0) {
        throw 'DX12 validation was disabled or reported problems/missing messages.'
    }
    $result.validation = $validation[0].data
    $result.passed = $true
}
catch { $result.error = $_.Exception.Message; throw }
finally {
    if ($process -and (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)")) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    $result.processStopped = !$process -or !(Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)")
    $env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY = $savedRuntime
    $env:CREATOR_DX12_VALIDATION = $savedValidation
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding utf8
}
