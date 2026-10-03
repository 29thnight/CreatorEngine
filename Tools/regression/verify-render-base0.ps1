#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('dx12','vulkan')][string[]]$Backend = @('dx12'),
    [switch]$Phase49,
    [switch]$ReplayExtensions,
    [switch]$ReplayDiagnosticsRun,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Scene = 'LX_CookFixture.creator',
    [string]$Python = 'python',
    [ValidateRange(1,100)][int]$WarmupFrames = 8
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (($Backend -contains 'vulkan') -and !$Phase49) {
    throw 'Vulkan comparison belongs to PHASE 4.9; use explicit -Phase49 for its diagnostic runs only.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new artifact directory; previous baselines are preserved' }
New-Item -ItemType Directory -Path $output | Out-Null
# Optional diagnostics use their own processes, captures and failure report.
# Always establish the baseline independently before attempting the extension.
if ($ReplayExtensions -and !$ReplayDiagnosticsRun) {
    $baselineArgs=@{Configuration=$Configuration; Backend=$Backend; Phase49=$Phase49;
        FixtureProject=$FixtureProject; Scene=$Scene; Python=$Python; WarmupFrames=$WarmupFrames}
    & $PSCommandPath @baselineArgs -OutputDirectory "$output/baseline"
    $baselineNativeExitCode=Get-Variable LASTEXITCODE -ValueOnly -ErrorAction SilentlyContinue
    $baseline=Get-Content "$output/baseline/result.json" -Raw | ConvertFrom-Json
    $extensionError=$null
    try {
        & $PSCommandPath @baselineArgs -OutputDirectory "$output/replay-extension" -ReplayExtensions -ReplayDiagnosticsRun
    } catch { $extensionError=$_.Exception.Message }
    finally { $global:LASTEXITCODE=$baselineNativeExitCode }
    $baseline.replayExtensionsRequested=$true
    $baseline | Add-Member -NotePropertyName replayExtensionResult -NotePropertyValue "$output/replay-extension/result.json"
    $baseline | Add-Member -NotePropertyName replayExtensionError -NotePropertyValue $extensionError
    $baseline | ConvertTo-Json -Depth 20 | Set-Content "$output/result.json" -Encoding utf8
    Copy-Item -LiteralPath "$output/baseline/source-hashes.json" -Destination "$output/source-hashes.json"
    if ($extensionError) { Write-Warning "Optional replay diagnostics failed: $extensionError" }
    Write-Output "BASE0_ARTIFACT_GATES_OK $Configuration $output complete=$($baseline.complete)"
    return
}
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$artifactTool = Join-Path $PSScriptRoot 'base0_artifacts.py'
$cameraReplayTool = Join-Path $PSScriptRoot 'base0_camera_replay.py'
$drawReplayTool = Join-Path $PSScriptRoot 'base0_draw_replay.py'
$latticeReplayTool = Join-Path $PSScriptRoot 'base0_lattice_replay.py'
$timingProbe=Join-Path $repo "Bin/x64-$Configuration/Tools/Base0VulkanTimingProbe/Base0VulkanTimingProbe.exe"
$sources = @(Get-ChildItem "$repo/Engine", "$repo/Editor", "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File |
    Where-Object Extension -In '.cpp','.h','.slang','.vcxproj','.props','.targets')
$sources += Get-Item $PSCommandPath, $artifactTool, $cameraReplayTool, $drawReplayTool, $latticeReplayTool,
    (Join-Path $PSScriptRoot 'complete-render-base0.ps1'),
    (Join-Path $PSScriptRoot 'base0_vulkan_timing_probe.cpp'),
    (Join-Path $PSScriptRoot 'generate-film-sensitivity-upload.py'),
    (Join-Path $PSScriptRoot 'Base0VulkanTimingProbe.vcxproj')
$sourceHashes = @($sources | Sort-Object FullName -Unique | ForEach-Object {
    @{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$sourceHashes | ConvertTo-Json -Depth 4 | Set-Content "$output/source-hashes.json" -Encoding utf8
# WMI AdapterRAM is a 32-bit field; actual VRAM comes from the RHI budget snapshot.
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion |
    ConvertTo-Json -Depth 4 | Set-Content "$output/hardware.json" -Encoding utf8
$report = [ordered]@{
    schemaVersion=1; configuration=$Configuration; contract='asset-reconstructed-sealed-input-v1'
    executable=$exe; executableSha256=(Get-FileHash $exe -Algorithm SHA256).Hash
    runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll') -Algorithm SHA256).Hash
    warmupFrames=$WarmupFrames; validation='gpu'; frameKind='real'; iblBrdfSamples=1024; iblEnvironmentSamples=4096
    deferred=@('PHASE 4.9: RenderDoc capture, resource inspection, and cross-backend pixel comparison')
    completionContract='dx12-static-baseline-v2'; replayExtensionsRequested=[bool]$ReplayExtensions
    knownGaps=@('Optional generic sealed frame packet replay','MAT-9 SSS/transmission quality','MAT-9 texture/normal/route/area-light acceptance',
        'MAT-9 moving-camera/tier/cold-warm performance')
    runs=@(); failures=@(); complete=$false; graphFixturesPassed=$false; vulkanTimingFixturePassed=$false
}
$previousValidation=$env:CREATOR_DX12_VALIDATION
$previousVulkanValidation=$env:CREATOR_VULKAN_VALIDATION
$env:CREATOR_DX12_VALIDATION='gpu'
$env:CREATOR_VULKAN_VALIDATION='on'
try {
    & $Python (Join-Path $PSScriptRoot 'generate-film-sensitivity-upload.py') --check > "$output/film-table-mirror.log"
    if ($LASTEXITCODE -ne 0) { throw 'Sensitivity upload mirror differs from canonical shader values' }
    if ($Phase49 -and ($Backend -contains 'vulkan')) {
        if (!(Test-Path $timingProbe)) { throw 'Build Tools/regression/Base0VulkanTimingProbe.vcxproj first' }
        $report.timingProbeSha256=(Get-FileHash $timingProbe -Algorithm SHA256).Hash
        $probePath=$env:PATH
        try {
            $dependencyFolder=if($Configuration -eq 'Debug'){'debug/bin'}else{'bin'}
            $env:PATH="$repo/vcpkg_installed/x64-windows/$dependencyFolder;$(Split-Path $exe);$probePath"
            $probe=Start-Process $timingProbe -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput "$output/vulkan-timing.stdout.log" -RedirectStandardError "$output/vulkan-timing.stderr.log"
            if (!$probe.WaitForExit(30000)) { $probe.Kill(); $probe.WaitForExit(); throw 'Vulkan timing fixture timeout' }
            if ($probe.ExitCode -ne 0 -or !(Select-String -LiteralPath "$output/vulkan-timing.stdout.log" -SimpleMatch -Pattern 'BASE0_VULKAN_TIMING_OK validation=0 repeats=3')) {
                throw 'Vulkan timing fixture failed'
            }
            $report.vulkanTimingFixturePassed=$true
            $probe.Dispose()
        } finally { $env:PATH=$probePath }
    }
    foreach ($api in $Backend) {
        foreach ($repeat in 0..1) {
            $case=Join-Path $output "$api-$repeat"
            $project=Join-Path $case 'Project'
            New-Item -ItemType Directory -Path $project | Out-Null
            foreach ($folder in @('Assets','ProjectSetting','Saved')) {
                $source=Join-Path $FixtureProject $folder
                if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $project -Recurse }
            }
            Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Destination "$project/Assets/Shaders" -Recurse -Force
            $settings="$project/ProjectSetting/EngineSettings.asset"
            if (!(Test-Path $settings)) { throw 'Fixture must include explicit EngineSettings.asset' }
            $text=Get-Content $settings -Raw
            $text=[regex]::Replace($text,'(?m)(backend:\s*)(dx12|vulkan)\b',('${1}'+$api))
            [IO.File]::WriteAllText($settings,$text,[Text.UTF8Encoding]::new($false))
            # The same native graph fixtures are a separate gate from live pixels.
            if ($api -eq 'dx12' -and $repeat -eq 0) {
                $commands="$case/graph-fixtures.txt"
                $fixtureArgs=if($ReplayExtensions){' replay-extensions'}else{''}
                [IO.File]::WriteAllText($commands,"dx12.rendergraph$fixtureArgs`nquit`n")
                $fixtureProcess=Start-Process $exe -ArgumentList @('--development-project',$project,
                    '--commandlet-script',$commands,'--result-file',"$case/graph-fixtures.results.jsonl") `
                    -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput "$case/graph-fixtures.stdout.log" -RedirectStandardError "$case/graph-fixtures.stderr.log"
                if (!$fixtureProcess.WaitForExit(120000)) {
                    $fixtureProcess.Kill(); $fixtureProcess.WaitForExit()
                    throw 'Native graph fixture timeout'
                }
                $fixtureResults=@(Get-Content "$case/graph-fixtures.results.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
                if ($fixtureProcess.ExitCode -ne 0 -or $fixtureResults.Count -ne 2 -or
                    $fixtureResults[0].status -ne 'succeeded' -or
                    $fixtureResults[0].data.log -notmatch 'BASE0_GRAPH_FIXTURES_OK' -or
                    ($ReplayExtensions -and ($fixtureResults[0].data.log -notmatch 'BASE0_CAMERA_REPLAY_OK' -or
                    $fixtureResults[0].data.log -notmatch 'BASE0_DRAW_REPLAY_OK' -or
                    $fixtureResults[0].data.log -notmatch 'BASE0_LATTICE_REPLAY_OK'))) {
                    throw 'Native graph fixtures failed or binary lacks the BASE-0 fixture consumer'
                }
                $report.graphFixturesPassed=$true
                $fixtureProcess.Dispose()
            }
            $inputs=@(Get-ChildItem "$project/Assets", "$project/ProjectSetting" -Recurse -File | ForEach-Object {
                @{path=$_.FullName.Substring($project.Length+1);sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}
            })
            $inputs | ConvertTo-Json -Depth 4 | Set-Content "$case/preparation-input-files.json" -Encoding utf8
            $proc=$null
            $run=[ordered]@{backend=$api;repeat=$repeat;directory=$case;passed=$false;captures=@()
                tuningIdentity=[regex]::Replace($text,'(?m)(backend:\s*)(dx12|vulkan)\b','${1}baseline-backend')}
            try {
                $start=[Diagnostics.ProcessStartInfo]::new($exe)
                foreach ($arg in @('--development-project',$project,'--command-service','--smoke-offscreen')) {
                    $start.ArgumentList.Add($arg)
                }
                $start.WorkingDirectory=Split-Path $exe
                $start.UseShellExecute=$false; $start.CreateNoWindow=$true
                $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
                $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
                $proc=[Diagnostics.Process]::Start($start)
                $stdout=$proc.StandardOutput.ReadToEndAsync(); $stderr=$proc.StandardError.ReadToEndAsync()
                $run.pid=$proc.Id
                $deadline=[DateTime]::UtcNow.AddSeconds(180)
                do {
                    if ($proc.HasExited) { throw "Editor exited during startup: $($proc.ExitCode)" }
                    $endpointPath="$project/Library/CommandService/endpoint.json"
                    $endpoint=if(Test-Path $endpointPath){Get-Content $endpointPath -Raw | ConvertFrom-Json}else{$null}
                    if([DateTime]::UtcNow -gt $deadline){throw 'Editor endpoint timeout'}
                    Start-Sleep -Milliseconds 100
                } until($endpoint -and $endpoint.pid -eq $proc.Id)
                $base="http://127.0.0.1:$($endpoint.port)"
                $headers=@{Authorization="Bearer $($endpoint.token)"}
                function Cmd([string]$Name,[string[]]$Arguments=@(),[switch]$AllowFailure) {
                    $body=@{command=$Name;args=@($Arguments);mode='async'} | ConvertTo-Json -Compress
                    $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 650
                    if($Name -eq 'quit'){return}
                    # Vulkan's first immutable LX compute PSO can spend minutes in
                    # the driver compiler. This is readiness, outside capture timing.
                    $deadline=[DateTime]::UtcNow.AddSeconds(660)
                    if($r.PSObject.Properties['operationId']){
                        $poll=$r.poll
                        do {
                            Start-Sleep -Milliseconds 100
                            $r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 650
                            if([DateTime]::UtcNow -gt $deadline){throw "$Name operation timeout"}
                        }until($r.state -eq 'completed')
                    }
                    $r | ConvertTo-Json -Depth 40 -Compress | Add-Content "$case/responses.jsonl" -Encoding utf8
                    if($AllowFailure){return $r}
                    if($r.status -ne 'succeeded'){throw "$Name failed: $($r.code) $($r.message)"}
                    return $r.data
                }
                Cmd 'play.foreground_override' @('off') | Out-Null
                Cmd 'scene.switch' @("$project/Assets/Scenes/$Scene") | Out-Null
                # Scene activation can precede camera publication and cold material readiness.
                Cmd 'render.live.fence' @('600') | Out-Null
                Cmd 'camera.editor' @('follow','on') | Out-Null
                Cmd 'render.environment' @('background','off') | Out-Null
                Cmd 'profile.pause' | Out-Null
                foreach($frame in 1..$WarmupFrames){Cmd 'render.live.fence' @('600') | Out-Null}
                # Cold import may advance model sidecar generation. Freeze the
                # current prepared input, while preserving raw source immutability.
                $preparationChanges=@($inputs | Where-Object {
                    $p=Join-Path $project $_.path
                    !(Test-Path -LiteralPath $p) -or (Get-FileHash -LiteralPath $p).Hash -ne $_.sha256
                })
                if(@($preparationChanges | Where-Object { $_.path -like 'Assets*' -and $_.path -notlike '*.meta' }).Count){
                    throw 'Raw fixture assets changed during preparation'
                }
                $run.preparationMetaChanges=@($preparationChanges | Where-Object { $_.path -like 'Assets*.meta' } | ForEach-Object path)
                $inputs=@(Get-ChildItem "$project/Assets", "$project/ProjectSetting" -Recurse -File | ForEach-Object {
                    @{path=$_.FullName.Substring($project.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
                })
                $inputs | ConvertTo-Json -Depth 4 | Set-Content "$case/input-files.json" -Encoding utf8
                $environment=Cmd 'render.environment' @('status')
                $environment | ConvertTo-Json -Depth 5 | Set-Content "$case/environment.json" -Encoding utf8
                foreach($shot in 0..1){
                    $captureMode=if($ReplayExtensions){'controlled-lattice-replay'}else{'controlled'}
                    $captureArgs=@("$case/capture-$shot",'editor',$captureMode)
                    $cameraSource=$null
                    if($ReplayExtensions -and $api -eq 'dx12' -and ($shot -gt 0 -or $repeat -gt 0)){
                        $cameraSource=Join-Path $output 'dx12-0/capture-0/camera-input.bin'
                        $captureArgs += $cameraSource
                        $captureArgs += (Join-Path $output 'dx12-0/capture-0/draw-input.bin')
                        $captureArgs += (Join-Path $output 'dx12-0/capture-0/lattice-input.bin')
                    }
                    Cmd 'render.live.capture' $captureArgs | Out-Null
                    if($cameraSource){
                        $meta=Get-Content "$case/capture-$shot/manifest.json" -Raw | ConvertFrom-Json
                        if(([string]$meta.cameraInputReplayed -notin @('True','1')) -or
                            (Get-FileHash $cameraSource).Hash -ne (Get-FileHash "$case/capture-$shot/camera-input.bin").Hash){
                            throw 'Camera replay input was not consumed exactly'
                        }
                        if(([string]$meta.drawInputReplayed -notin @('True','1')) -or
                            (Get-FileHash (Join-Path $output 'dx12-0/capture-0/draw-input.bin')).Hash -ne
                            (Get-FileHash "$case/capture-$shot/draw-input.bin").Hash){
                            throw 'Draw replay input was not consumed exactly'
                        }
                        if(([string]$meta.latticeInputReplayed -notin @('True','1')) -or
                            (Get-FileHash (Join-Path $output 'dx12-0/capture-0/lattice-input.bin')).Hash -ne
                            (Get-FileHash "$case/capture-$shot/lattice-input.bin").Hash){
                            throw 'Lattice replay input was not consumed exactly'
                        }
                    }
                    $run.captures += "$case/capture-$shot"
                    Cmd 'render.live.fence' @('600') | Out-Null
                }
                if($ReplayExtensions -and $api -eq 'dx12' -and $repeat -eq 0){
                    & $Python $cameraReplayTool prepare "$case/capture-0" "$case/camera-cases" > "$case/camera-cases.log"
                    if($LASTEXITCODE -ne 0){throw 'Camera input preparation failed'}
                    foreach($bad in @('truncated','checksum','version','target','extent','nonfinite')){
                        $rejection=Cmd 'render.live.capture' @("$case/rejected-$bad",'editor','controlled',"$case/camera-cases/$bad.bin") -AllowFailure
                        $expectedCode=if($bad -eq 'extent'){'render.pbr.capture.failed'}else{'render.pbr.capture.rejected'}
                        if($rejection.status -eq 'succeeded' -or $rejection.code -ne $expectedCode){
                            throw "Camera mutation was not rejected at the expected boundary: $bad"
                        }
                        if($bad -ne 'extent' -and (Test-Path "$case/rejected-$bad")){throw 'Invalid packet created capture output'}
                    }
                    Cmd 'render.live.capture' @("$case/shifted-camera",'editor','controlled',"$case/camera-cases/shifted-camera.bin") | Out-Null
                    & $Python $cameraReplayTool verify "$case/capture-0" "$case/shifted-camera" "$case/camera-cases/shifted-camera.bin" > "$case/shifted-camera.log"
                    if($LASTEXITCODE -ne 0){throw 'Actual camera replay consumer failed'}
                    Cmd 'render.live.capture' @("$case/replayed-clock",'editor','controlled',"$case/camera-cases/clock.bin") | Out-Null
                    $clockMeta=Get-Content "$case/replayed-clock/manifest.json" -Raw | ConvertFrom-Json
                    if($clockMeta.totalSeconds -ne 17.25 -or [Math]::Abs($clockMeta.deltaSeconds - (1.0/60)) -gt 0.0000001 -or
                        (Get-FileHash "$case/replayed-clock/camera-input.bin").Hash -ne (Get-FileHash "$case/camera-cases/clock.bin").Hash){
                        throw 'Nonzero render clock replay was ignored'
                    }
                    & $Python $drawReplayTool prepare "$case/capture-0" "$case/draw-cases" > "$case/draw-cases.log"
                    if($LASTEXITCODE -ne 0){throw 'Draw replay preparation failed'}
                    foreach($bad in @('truncated','checksum','version','count','route','pose-count','nonfinite','asset','geometry')){
                        $rejection=Cmd 'render.live.capture' @("$case/rejected-draw-$bad",'editor','controlled',
                            "$case/capture-0/camera-input.bin","$case/draw-cases/$bad.bin") -AllowFailure
                        $expectedCode=if($bad -in @('asset','geometry')){'render.pbr.capture.failed'}else{'render.pbr.capture.rejected'}
                        if($rejection.status -eq 'succeeded' -or $rejection.code -ne $expectedCode){
                            throw "Draw mutation accepted or rejected at wrong boundary: $bad"
                        }
                        if($bad -notin @('asset','geometry') -and (Test-Path "$case/rejected-draw-$bad")){
                            throw 'Invalid draw file created output directory'
                        }
                    }
                    Cmd 'render.live.capture' @("$case/shifted-world",'editor','controlled',
                        "$case/capture-0/camera-input.bin","$case/draw-cases/shifted-world.bin") | Out-Null
                    & $Python $drawReplayTool verify "$case/capture-0" "$case/shifted-world" "$case/draw-cases/shifted-world.bin" > "$case/shifted-world.log"
                    if($LASTEXITCODE -ne 0){throw 'Actual draw replay consumer failed'}
                    $run.drawMutationsPassed=$true
                    & $Python $latticeReplayTool prepare "$case/capture-0" "$case/lattice-cases" > "$case/lattice-cases.log"
                    if($LASTEXITCODE -ne 0){throw 'Lattice replay preparation failed'}
                    foreach($bad in @('truncated','checksum','version','count','coverage','parameter-count','type','nonfinite',
                        'program','graph-asset','texture-content','texture-asset','parameter-id','parameter-type')){
                        $drawSource=if($bad -eq 'program'){"$case/draw-cases/shifted-world.bin"}else{"$case/capture-0/draw-input.bin"}
                        $rejection=Cmd 'render.live.capture' @("$case/rejected-lattice-$bad",'editor','controlled',
                            "$case/capture-0/camera-input.bin",$drawSource,"$case/lattice-cases/$bad.bin") -AllowFailure
                        $liveRejections=@('program','graph-asset','texture-content','texture-asset','parameter-id','parameter-type')
                        $expectedCode=if($bad -in $liveRejections){'render.pbr.capture.failed'}else{'render.pbr.capture.rejected'}
                        if($rejection.status -eq 'succeeded' -or $rejection.code -ne $expectedCode){
                            throw "Lattice mutation accepted or rejected at wrong boundary: $bad"
                        }
                        if($bad -notin $liveRejections -and (Test-Path "$case/rejected-lattice-$bad")){
                            throw 'Invalid Lattice file created output directory'
                        }
                    }
                    Cmd 'render.live.capture' @("$case/changed-material",'editor','controlled',
                        "$case/capture-0/camera-input.bin","$case/capture-0/draw-input.bin","$case/lattice-cases/changed-material.bin") | Out-Null
                    & $Python $latticeReplayTool verify "$case/capture-0" "$case/changed-material" "$case/lattice-cases/changed-material.bin" > "$case/changed-material.log"
                    if($LASTEXITCODE -ne 0){throw 'Actual Lattice replay consumer failed'}
                    $run.latticeMutationsPassed=$true
                    Cmd 'render.live.capture' @("$case/after-replay",'editor','controlled') | Out-Null
                    & $Python $artifactTool compare "$case/capture-0" "$case/after-replay" "$case/replay-isolation" > "$case/replay-isolation.log"
                    if($LASTEXITCODE -ne 0){throw 'Replay leaked into later live frames'}
                    $run.cameraMutationsPassed=$true
                }
                $captureManifest=Get-Content "$case/capture-0/manifest.json" -Raw | ConvertFrom-Json
                $run.environmentSha256=(Get-FileHash -LiteralPath $captureManifest.skyBoxPath -Algorithm SHA256).Hash
                Cmd 'dx12.live' | ConvertTo-Json -Depth 30 | Set-Content "$case/live-status.json" -Encoding utf8
                $validation=Cmd 'dx12.validation'
                $validation | ConvertTo-Json -Depth 10 | Set-Content "$case/validation.json" -Encoding utf8
                if($api -eq 'dx12' -and (!$validation.layerEnabled -or $validation.mode -ne 'gpu' -or
                    $validation.problems -ne 0 -or $validation.droppedMessages -ne 0)){
                    throw 'DX12 validation must be enabled, clean, and fully drained'
                }
                Cmd 'log.flush' | Out-Null
                Cmd 'quit' | Out-Null
                if(!$proc.WaitForExit(30000)){throw 'Normal shutdown timeout'}
                if($proc.ExitCode -ne 0){throw "Normal shutdown failed: $($proc.ExitCode)"}
                $changedInputs=@($inputs | Where-Object {
                    $inputPath=Join-Path $project $_.path
                    !(Test-Path -LiteralPath $inputPath) -or
                        (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash -ne $_.sha256
                })
                # Settings are persisted by the Editor; assets must remain immutable.
                $changedAssets=@($changedInputs | Where-Object { $_.path -like 'Assets*' })
                if($changedAssets.Count){throw 'Fixture assets changed during capture'}
                $run.assetIdentity=($inputs | Where-Object { $_.path -like 'Assets*' } |
                    Sort-Object path | ConvertTo-Json -Depth 4 -Compress)
                & $Python $artifactTool compare "$case/capture-0" "$case/capture-1" "$case/repeatability" > "$case/comparison.stdout.json"
                if($LASTEXITCODE -ne 0){throw 'Same-process artifact gate failed'}
                & $Python $artifactTool mutations "$case/capture-0" "$case/mutations.json" > "$case/mutations.stdout.json"
                if($LASTEXITCODE -ne 0){throw 'Artifact mutation gate failed'}
                $run.passed=$true
            } catch {
                $report.failures += "$api/$repeat`: $($_.Exception.Message)"
                $run.error=$_.Exception.Message
            } finally {
                if($proc){
                    $run.forcedTermination=!$proc.HasExited
                    if($run.forcedTermination){$proc.Kill();$proc.WaitForExit()}
                    $run.exitCode=$proc.ExitCode
                    [IO.File]::WriteAllText("$case/editor.stdout.log",$stdout.GetAwaiter().GetResult())
                    [IO.File]::WriteAllText("$case/editor.stderr.log",$stderr.GetAwaiter().GetResult())
                    $proc.Dispose()
                }
                $report.runs += $run
                $report | ConvertTo-Json -Depth 20 | Set-Content "$output/result.json" -Encoding utf8
            }
            # An unsuccessful live run is diagnostic evidence, not permission
            # to repeat a crashing Editor and open another crash-report window.
            if (!$run.passed) { throw "$api/$repeat baseline run failed; see $case" }
        }
        $pair=@($report.runs | Where-Object { $_.backend -eq $api -and $_.passed })
        if($pair.Count -eq 2){
            if($pair[0].environmentSha256 -ne $pair[1].environmentSha256){throw 'Environment changed between processes'}
            if($pair[0].assetIdentity -ne $pair[1].assetIdentity){throw 'Asset identity changed between processes'}
            if($pair[0].tuningIdentity -ne $pair[1].tuningIdentity){throw 'Tuning changed between processes'}
            & $Python $artifactTool compare $pair[0].captures[0] $pair[1].captures[0] "$output/$api-process-repeatability" > "$output/$api-process-comparison.stdout.json"
            if($LASTEXITCODE -ne 0){$report.failures += "$api independent-process artifact gate failed"}
        }
    }
    $dx=@($report.runs | Where-Object { $_.backend -eq 'dx12' -and $_.repeat -eq 0 -and $_.passed })
    $vk=@($report.runs | Where-Object { $_.backend -eq 'vulkan' -and $_.repeat -eq 0 -and $_.passed })
    if($Phase49 -and $dx.Count -eq 1 -and $vk.Count -eq 1){
        if($dx[0].environmentSha256 -ne $vk[0].environmentSha256){throw 'Backend environment identity mismatch'}
        if($dx[0].assetIdentity -ne $vk[0].assetIdentity){throw 'Backend asset identity mismatch'}
        if($dx[0].tuningIdentity -ne $vk[0].tuningIdentity){throw 'Backend tuning identity mismatch'}
        & $Python $artifactTool compare $dx[0].captures[0] $vk[0].captures[0] "$output/backend-parity" > "$output/backend-comparison.stdout.json"
        if($LASTEXITCODE -ne 0){$report.failures += 'Cross-backend artifact gate failed'}
    }
    $changed=@($sourceHashes | Where-Object { !(Test-Path $_.path) -or (Get-FileHash $_.path -Algorithm SHA256).Hash -ne $_.sha256 })
    $report.sourceChanges=$changed.Count
    if((Get-FileHash $exe -Algorithm SHA256).Hash -ne $report.executableSha256 -or
        (Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll') -Algorithm SHA256).Hash -ne $report.runtimeSha256){
        $report.failures += 'Executable changed during baseline execution'
    }
    if($report.Contains('timingProbeSha256') -and (Get-FileHash $timingProbe -Algorithm SHA256).Hash -ne $report.timingProbeSha256){
        $report.failures += 'Timing fixture binary changed during baseline execution'
    }
    if($changed.Count){$report.failures += 'Sources changed during baseline execution'}
    $report.staticFixtureGatesComplete=$report.failures.Count -eq 0 -and $report.graphFixturesPassed -and
        @($report.runs | Where-Object { $_.backend -eq 'dx12' -and $_.passed }).Count -eq 2
    $report.renderDocResourcesVerified=$false # This artifact harness cannot close PHASE 4.9.
    $report.cameraClockReplayVerified=[bool]$ReplayExtensions -and $report.staticFixtureGatesComplete -and
        @($report.runs | Where-Object { $_.backend -eq 'dx12' -and $_.repeat -eq 0 -and $_.cameraMutationsPassed }).Count -eq 1
    # Asset reconstruction proves this static fixture, not generic sealed-packet replay.
    $report.genericPacketReplayVerified=$false
    $report.selectedTransformReplayVerified=[bool]$ReplayExtensions -and $report.staticFixtureGatesComplete -and
        @($report.runs | Where-Object { $_.backend -eq 'dx12' -and $_.repeat -eq 0 -and $_.drawMutationsPassed }).Count -eq 1
    $report.animatedLivePoseReplayVerified=$false # Static LX fixture has no bone palette; native ownership proof is separate.
    $report.latticeInstanceReplayVerified=[bool]$ReplayExtensions -and $report.staticFixtureGatesComplete -and
        @($report.runs | Where-Object { $_.backend -eq 'dx12' -and $_.repeat -eq 0 -and $_.latticeMutationsPassed }).Count -eq 1
    $report.shaderMetaMaterialReplayVerified=$false
    $report.forwardMaterialReplayVerified=$false
    # One configuration's baseline gate, not the joint Debug/Release phase closure.
    $report.complete=$report.staticFixtureGatesComplete
    $report.completionScope=if($ReplayDiagnosticsRun){'optional-replay-diagnostics'}else{'single-configuration-baseline'}
    $report.phaseComplete=$false # Requires both current configurations and their cross-configuration comparison.
    $report.artifactGatesPassed=$report.failures.Count -eq 0
    $report | ConvertTo-Json -Depth 20 | Set-Content "$output/result.json" -Encoding utf8
    if($report.failures.Count){throw ($report.failures -join "`n")}
    Write-Output "BASE0_ARTIFACT_GATES_OK $Configuration $output complete=$($report.complete)"
} catch {
    if (!$report.failures.Count) { $report.failures += $_.Exception.Message }
    $report.complete=$false
    $report | ConvertTo-Json -Depth 20 | Set-Content "$output/result.json" -Encoding utf8
    throw
} finally {
    $env:CREATOR_DX12_VALIDATION=$previousValidation
    $env:CREATOR_VULKAN_VALIDATION=$previousVulkanValidation
}
