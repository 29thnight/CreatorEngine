# PHASE 4 W0/W2/W3/W4/W5/W6/W7-normal/UV: product-frame capture, shared PBR and backend defaults.
# Captures are observations, not W9 visual acceptance or cross-backend goldens.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [ValidateSet('dx12', 'vulkan')][string[]]$Backend = @('dx12', 'vulkan'),
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path ([IO.Path]::GetFullPath($Work)) ('creator-pbr-' + [guid]::NewGuid().ToString('N'))
$settings = Join-Path $root 'Dynamic_CPP\ProjectSetting\EngineSettings.asset'
$utf8 = [Text.UTF8Encoding]::new($false)
$original = $null
$process = $null

function Invoke-Editor([string]$Name, [string[]]$Commands, [int]$ExpectedExit = 0) {
    $scenario = Join-Path $run "$Name.txt"
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", $utf8)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $Editor).Path
    $start.ArgumentList.Add('--commandlet-script')
    $start.ArgumentList.Add($scenario)
    $resultPath = Join-Path $run "$Name.results.jsonl"
    $start.ArgumentList.Add('--result-file')
    $start.ArgumentList.Add($resultPath)
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $script:process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $finished) { $process.Kill(); $process.WaitForExit() }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run "$Name.stdout.txt"), $stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $run "$Name.stderr.txt"), $stderr, $utf8)
    $exitCode = $process.ExitCode
    $process.Dispose()
    $script:process = $null
    if (-not $finished) { throw "$Name timed out; artifacts: $run" }
    if ($exitCode -ne $ExpectedExit) { throw "$Name exit=$exitCode expected=$ExpectedExit; artifacts: $run" }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) { throw "$Name wrote stderr; artifacts: $run" }
    $results = @(Read-CommandResults $resultPath)
    if ($results.Count -ne $Commands.Count) { throw "$Name terminal result count mismatch; artifacts: $run" }
    for ($i = 0; $i -lt $Commands.Count; $i++) {
        if ($results[$i].command -ne ($Commands[$i] -split ' ', 2)[0]) {
            throw "$Name terminal result order mismatch at $i"
        }
    }
    if ($ExpectedExit -eq 0 -and @($results | Where-Object status -ne 'succeeded').Count) {
        throw "$Name has unsuccessful command results; artifacts: $run"
    }
    return $results
}

function Assert-Capture([string]$Directory, [string]$ExpectedBackend, [string[]]$Models) {
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.source -ne 'product-live' -or $manifest.backend -ne $ExpectedBackend -or
        -not $manifest.finite -or $manifest.validationCount -ne 0 -or
        $manifest.frameId -le $manifest.requestedAfterFrameId -or
        $manifest.draws.Count -eq 0 -or $manifest.attachments.Count -ne 7) {
        throw "Invalid product capture: $Directory"
    }
    foreach ($model in $Models) {
        $meta = [IO.File]::ReadAllText((Join-Path $root "Dynamic_CPP\Assets\Models\$model.glb.meta"))
        $id = [regex]::Match($meta, '(?m)^assetId:\s*(\S+)').Groups[1].Value
        if (-not $id -or $id -notin @($manifest.draws.modelId)) {
            throw "Capture omits visible model $model ($id): $Directory"
        }
    }
    foreach ($draw in $manifest.draws) {
        if ($draw.modelGeneration -le 0 -or $draw.shaderMetaGeneration -le 0 -or
            $draw.propertyBytes.Count -eq 0) { throw "Unsealed material/model: $Directory" }
        if (@($draw.textures | Where-Object property -EQ 'aoMap').Count -ne 1 -or
            @($draw.textures | Where-Object { $_.space -ne 0 -or $_.register -lt 16 -or $_.register -ge 128 }).Count -ne 0) {
            throw "Missing AO/reflected material texture range: $Directory"
        }
    }
    foreach ($attachment in $manifest.attachments) {
        $size = (Get-Item -LiteralPath (Join-Path $Directory $attachment.file)).Length
        $expected = [long]$attachment.width * $attachment.height * $attachment.channels * 4
        if ($size -ne $expected -or $attachment.nonfinite -ne 0) {
            throw "Invalid $($attachment.name) readback: $Directory"
        }
    }
    $depth = $manifest.attachments | Where-Object name -EQ 'depth'
    $hdr = $manifest.attachments | Where-Object name -EQ 'preToneHdr'
    if ($depth.min -ge 1 -or $depth.min -eq $depth.max -or $hdr.rgbMax -le 0) {
        throw "Empty depth/HDR capture: $Directory"
    }
}

try {
    if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
        throw 'Close the running Editor before this test; it temporarily selects the startup backend.'
    }
    New-Item -ItemType Directory -Path $run | Out-Null
    $original = [IO.File]::ReadAllBytes($settings)
    $text = $utf8.GetString($original)
    $backendPattern = '(?m)(^render:\r?\n\s{2}backend: )\w+'
    if ([regex]::Matches($text, $backendPattern).Count -ne 1) { throw 'Runtime backend setting is ambiguous.' }
    foreach ($api in $Backend) {
        [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, "`${1}$api"), $utf8)
        $primitive = Join-Path $run "$api-primitives"
        $gunner = Join-Path $run "$api-gunner"
        $commands = @(
            "scene.switch `"$root/Dynamic_CPP/Assets/Scenes/FT_Primitives.creator`"",
            'wait 30',
            "render.pbr.capture `"$primitive`" game",
            "model.loadcached `"$root/Dynamic_CPP/Assets/Models/Gunner_F_Mythic.glb`"",
            'model.place Gunner_F_Mythic',
            'object.transform Gunner_F_Mythic 0 0 2 0 180 0 0.025 0.025 0.025',
            'wait 30',
            "render.pbr.capture `"$gunner`" game",
            'quit')
        $results = @(Invoke-Editor $api $commands)
        $captures = @($results | Where-Object command -eq 'render.pbr.capture')
        if ($captures.Count -ne 2 -or @($captures | Where-Object { $_.data.frameId -le 0 }).Count) {
            throw "$api capture did not return two completed frames; artifacts: $run"
        }
        Assert-Capture $primitive $api @('Prim_Cube', 'Prim_Sphere', 'Prim_Cylinder')
        Assert-Capture $gunner $api @('Gunner_F_Mythic')
        Write-Output "$api product capture PASS: $primitive; $gunner"
    }
    # The paired harness owns both DX12 and Vulkan test devices; keep its Editor
    # host on DX12 independently of the final product-capture backend above.
    [IO.File]::WriteAllText($settings, [regex]::Replace($text, $backendPattern, '${1}dx12'), $utf8)
    $null = Invoke-Editor 'defaults' @('vk.shadow', 'vk.gbuffer', 'vk.forward', 'vk.deferred', 'quit')
    $null = Invoke-Editor 'forward-shade' @('dx12.forwardshade', 'quit')
    # Material codec/seal contracts moved out of the Editor registry in PHASE 14.5.
    $contractLog = Join-Path $run 'experiment-contract.log'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify-experiment-contract.ps1') -Configuration $Configuration *> $contractLog
    if ($LASTEXITCODE -ne 0) { throw "Standalone material contracts failed; log: $contractLog" }
    $contractCommands = @('experiment.matresolve', 'experiment.matmigrate', 'experiment.cooked')
    $null = Invoke-Editor 'material-contracts' ($contractCommands + 'quit')
    foreach ($name in @('parity', 'coverage', 'emission', 'transform', 'uv', 'occlusion')) {
        $results = @(Invoke-Editor $name @("render.pbr.$name", 'quit'))
        $data = Get-SucceededCommand $results "render.pbr.$name"
        if (-not $data.passed) { throw "PBR $name did not execute its verification" }
    }
    # A later successful test must not erase an earlier failure exit code.
    $results = @(Invoke-Editor 'negative-exit' @('render.livecheck 1 1', 'dx12.gbuffer', 'quit') 4)
    if ((Get-CommandResult $results 'render.livecheck').status -ne 'failed') {
        throw 'Negative exit probe did not exercise failure.'
    }
    $null = Get-SucceededCommand $results 'dx12.gbuffer'
    $results = @(Invoke-Editor 'negative-capture' @("render.pbr.capture `"$run`" game", 'quit') 4)
    $capture = Get-CommandResult $results 'render.pbr.capture'
    if ($capture.status -ne 'failed' -or $capture.code -ne 'render.pbr.capture.rejected') {
        throw 'Capture did not reject an existing output directory.'
    }
    Write-Output "PBR W0/W2/W3/W4/W5/W6/W7-normal/UV baseline PASS (W9 acceptance pending): $run"
}
finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    if ($null -ne $original) { [IO.File]::WriteAllBytes($settings, $original) }
}
