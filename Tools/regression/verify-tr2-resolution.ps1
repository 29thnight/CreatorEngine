#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [Parameter(Mandatory)][string]$OutputDirectory,
    [string[]]$Providers=@('fsr','dlss','xess'),
    [string[]]$Qualities=@('native-aa','quality','balanced','performance','ultra-performance'),
    [string[]]$Resolutions=@('960x640','1280x720','1600x900'), [switch]$SkipNative, [switch]$SkipSoloControl,
    [switch]$Resume, [switch]$RepeatFirstSoloControl)
$ErrorActionPreference='Stop'
if($Resume -and $RepeatFirstSoloControl){throw 'Repeated first-condition control requires a fresh matrix run.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if($Resume) {
    if(!(Test-Path "$out/Project/Assets/Scenes/TR0_Native_Workload.creator")){throw 'Resume requires the existing isolated project.'}
    $oldResult=Get-Content "$out/result.json" -Raw | ConvertFrom-Json
    $currentBinary=(Get-FileHash "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll").Hash
    if($oldResult.binarySha256 -ne $currentBinary){throw 'Cannot resume with a different runtime binary.'}
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    foreach($name in @('result.json','matrix.json','error.txt','stdout.log','stderr.log')) {
        if(Test-Path "$out/$name"){Copy-Item -LiteralPath "$out/$name" -Destination "$out/$name.before-$stamp"}
    }
    if(Test-Path "$out/error.txt"){Move-Item -LiteralPath "$out/error.txt" -Destination "$out/error-before-$stamp.txt"}
    $scenePath="$out/Project/Assets/Scenes/TR0_Native_Workload.creator"
} else {
if(Test-Path $out){throw 'Fresh output required'}
New-Item -ItemType Directory $out | Out-Null
# Preserve every raw float attachment with transparent NTFS compression.
# These are pixel checks; capture I/O timings are not performance measurements.
& compact.exe /C /Q $out | Out-Null
if($LASTEXITCODE){throw 'Cannot enable raw capture directory compression.'}
& "$repo/Tools/regression/new-temporal-base0-fixture.ps1" -OutputDirectory "$out/Project"
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$out/Project/Assets/Shaders" -Recurse
$scenePath="$out/Project/Assets/Scenes/TR0_Native_Workload.creator"
$scene=[IO.File]::ReadAllText($scenePath).Replace('m_name: MainCamera','m_name: Camera')
[IO.File]::WriteAllText($scenePath,$scene)
& python "$repo/Tools/regression/prepare-tr3-live-geometry.py" "$out/Project"
if($LASTEXITCODE){throw "Fixture geometry preparation failed"}
}
function Test-ReportPass([string]$path,[switch]$Native) {
    if(!(Test-Path -LiteralPath $path)){return $false}
    try {
        $value=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        if($Native){return $value.acceptanceStatus -eq 'PASS'}
        return $value.passed -eq $true
    } catch {return $false}
}
function Preserve-IncompleteCase([string]$path) {
    if(!(Test-Path -LiteralPath $path)){return}
    $source=[IO.Path]::GetFullPath($path)
    $destination=[IO.Path]::GetFullPath($path+'.before-resume-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $boundary=$out.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(!$source.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or
        !$destination.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing a case move outside the evidence root or through a reparse point.'
    }
    Move-Item -LiteralPath $source -Destination $destination
}
# Completed pixels alone do not cover a previous process's validation state.
$resumeCanReuseCases=$false
if($Resume -and (Test-Path "$out/validation.json")) {
    $previousValidation=Get-Content "$out/validation.json" -Raw | ConvertFrom-Json
    $resumeCanReuseCases=$previousValidation.layerEnabled -and $previousValidation.messages -eq 0 -and
        $previousValidation.problems -eq 0 -and $previousValidation.droppedMessages -eq 0
}
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$savedEnvironment=@{}
foreach($name in @('CREATOR_TEMPORAL_RUNTIME_DIRECTORY','CREATOR_DX12_VALIDATION','CREATOR_EDITOR_WORKSPACE_DIR','CREATOR_EDITOR_LEGACY_INI')) {
    $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY="$repo/Bin/x64-$Configuration/Editor/Temporal"
$env:CREATOR_DX12_VALIDATION='basic'
$env:CREATOR_EDITOR_WORKSPACE_DIR="$out/workspace"
$env:CREATOR_EDITOR_LEGACY_INI="$out/workspace/legacy.ini"
$arguments=@('--development-project',"$out/Project",'--command-service','--smoke-offscreen')
if(@($arguments | Where-Object { $_.Contains('"') }).Count){throw 'Arguments containing quotes are unsupported.'}
$quoted=@($arguments | ForEach-Object { '"'+$_+'"' })
$p=Start-Process $exe -ArgumentList $quoted -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log" -PassThru
$passed=$false
try {
    $endpoint="$out/Project/Library/CommandService/endpoint.json"
    $limit=[DateTime]::UtcNow.AddSeconds(90)
    do {
        if($p.HasExited){throw 'Editor exited before endpoint'}
        if(Test-Path $endpoint){$e=Get-Content $endpoint -Raw | ConvertFrom-Json;if($e.pid -eq $p.Id){break}}
        if([DateTime]::UtcNow -ge $limit){throw 'Endpoint timeout'}
        Start-Sleep -Milliseconds 250
    }while($true)
    $base='http://127.0.0.1:'+$e.port
    $headers=@{Authorization='Bearer '+$e.token}
    function Command([string]$name,[string[]]$arguments=@()) {
        $r=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json -Body (@{command=$name;args=@($arguments);mode='async'}|ConvertTo-Json -Compress) -TimeoutSec 660
        if($name -eq 'quit'){return}
        if($r.PSObject.Properties['operationId']) {
            $poll=$r.poll
            $limit=[DateTime]::UtcNow.AddSeconds(660)
            do {
                Start-Sleep -Milliseconds 100
                $r=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 660
                if([DateTime]::UtcNow -ge $limit){throw "$name timeout"}
            }until($r.state -eq 'completed')
        }
        @{command=$name;result=$r}|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out/setup.jsonl"
        if($r.status -ne 'succeeded'){throw "$name failed: $($r.message)"}
        return $r.data
    }
    $request=Command 'scene.switch' @("$out/Project/Assets/Scenes/TR0_Native_Workload.creator")
    $limit=[DateTime]::UtcNow.AddSeconds(120)
    do {
        $load=Command 'scene.load.status' @([string]$request.requestId)
        if($load.state -in @('failed','cancelled')){throw 'Scene load failed'}
        if($load.complete){break}
        if([DateTime]::UtcNow -ge $limit){throw 'Scene activation timeout'}
        Start-Sleep -Milliseconds 250
    }while($true)
    Command 'editor.viewport' @('game') | Out-Null
    Command 'editor.renderscale' @('off') | Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(120)
    do {
        $viewport=Command 'editor.viewport'
        $submitted=Command 'temporal.status'
        if($viewport.mode -eq 'game' -and $viewport.gameTarget -and $viewport.renderScaleMode -eq 'off' -and
            [uint64]$submitted.viewId -eq 2 -and $submitted.rendererObserved){break}
        if([DateTime]::UtcNow -ge $deadline){throw 'Actual Game viewport demand did not become ready'}
        Start-Sleep -Milliseconds 100
    }while($true)
    $matrix=[Collections.Generic.List[object]]::new()
    $repeatedSolo=$false
    foreach($resolution in $Resolutions) {
        if($resolution -notmatch '^(\d+)x(\d+)$'){throw 'Invalid host resolution'}
        $width=[int]$Matches[1];$height=[int]$Matches[2]
        $before=Command 'temporal.status'
        Command 'window.resize' @([string]$width,[string]$height) | Out-Null
        $deadline=[DateTime]::UtcNow.AddSeconds(90)
        do {
            $observed=Command 'temporal.status'
            if([uint64]$observed.lastRealFrameId -gt [uint64]$before.lastRealFrameId+4 -and !$observed.reset){break}
            if([DateTime]::UtcNow -ge $deadline){throw 'Resized view did not submit'}
            Start-Sleep -Milliseconds 100
        }while($true)
        if(!$SkipNative) {
            $case="$out/$resolution/native"
            if($resumeCanReuseCases -and (Test-ReportPass "$case/oracle.json" -Native)) {
                $matrix.Add(@{provider='none';host=$resolution;oracle="$case/oracle.json";passed=$true})
                $matrix | ConvertTo-Json -Depth 12 | Set-Content "$out/matrix.json"
            } else {
            Preserve-IncompleteCase $case
            & "$repo/Tools/temporal-validation/Invoke-TemporalStage.ps1" -EndpointFile $endpoint -Stage disable -OutputDirectory "$case/disable" | Out-Null
            Command 'render.temporal.motion.fixture' @("$case/fixture") | Out-Null
            & python "$repo/Tools/temporal-validation/Compare-TemporalMotion.py" --capture "$case/fixture" --fixture "$case/fixture/fixture.json" --output "$case/oracle.json" *> "$case/oracle.log"
            if($LASTEXITCODE){throw "Native route oracle failed: $case"}
            $matrix.Add(@{provider='none';host=$resolution;oracle="$case/oracle.json";passed=$true})
            $matrix | ConvertTo-Json -Depth 12 | Set-Content "$out/matrix.json"
            }
        }
        foreach($provider in $Providers) {
            foreach($quality in $Qualities) {
                $case="$out/$resolution/$provider-$quality"
                if($resumeCanReuseCases -and (Test-ReportPass "$case/oracle.json") -and
                    ($SkipSoloControl -or ((Test-ReportPass "$case/solo-oracle.json") -and (Test-ReportPass "$case/isolation.json")))) {
                    $matrix.Add(@{provider=$provider;quality=$quality;host=$resolution;companion=$true;oracle="$case/oracle.json";passed=$true})
                    $matrix | ConvertTo-Json -Depth 12 | Set-Content "$out/matrix.json"
                    continue
                }
                Preserve-IncompleteCase $case
                & "$repo/Tools/temporal-validation/Invoke-TemporalStage.ps1" -EndpointFile $endpoint -Stage upscale -Provider $provider -Quality $quality -OutputDirectory "$case/activate" | Out-Null
                Command 'render.temporal.motion.fixture' @("$case/fixture",'sdk-routes') | Out-Null
                & python "$repo/Tools/temporal-validation/Compare-TemporalResolution.py" --capture "$case/fixture" --output "$case/oracle.json" *> "$case/oracle.log"
                if($LASTEXITCODE){throw "SDK route/quality oracle failed: $case"}
                $matrix.Add(@{provider=$provider;quality=$quality;host=$resolution;companion=$true;oracle="$case/oracle.json";passed=$true})
                $matrix | ConvertTo-Json -Depth 12 | Set-Content "$out/matrix.json"
                if(!$SkipSoloControl) {
                    Command 'render.temporal.motion.fixture' @("$case/solo",'sdk-routes-solo') | Out-Null
                    & python "$repo/Tools/temporal-validation/Compare-TemporalResolution.py" --capture "$case/solo" --output "$case/solo-oracle.json" *> "$case/solo-oracle.log"
                    if($LASTEXITCODE){throw "Isolated SDK route oracle failed: $case"}
                    if($RepeatFirstSoloControl -and !$repeatedSolo) {
                        Command 'render.temporal.motion.fixture' @("$case/solo-repeat",'sdk-routes-solo') | Out-Null
                        & python "$repo/Tools/temporal-validation/Compare-TemporalResolution.py" --capture "$case/solo-repeat" --output "$case/solo-repeat-oracle.json" *> "$case/solo-repeat-oracle.log"
                        if($LASTEXITCODE){throw "Repeated solo route oracle failed: $case"}
                        & python "$repo/Tools/temporal-validation/Compare-TemporalViewIsolation.py" --isolated "$case/solo" --interleaved "$case/solo-repeat" --repeat --output "$case/repeat-isolation.json" *> "$case/repeat-isolation.log"
                        if($LASTEXITCODE){throw "Repeated solo pixels changed: $case"}
                        $repeatedSolo=$true
                    }
                    & python "$repo/Tools/temporal-validation/Compare-TemporalViewIsolation.py" --isolated "$case/solo" --interleaved "$case/fixture" --output "$case/isolation.json" *> "$case/isolation.log"
                    if($LASTEXITCODE){throw "SDK multi-view isolation failed: $case"}
                }
            }
        }
    }
    $validation=Command 'dx12.validation'
    $validation | ConvertTo-Json -Depth 10 | Set-Content "$out/validation.json"
    if(!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0){throw 'DX12 validation failed'}
    $passed=$true
}catch {
    $_ | Out-String | Set-Content "$out/error.txt"
    throw
}finally {
    if(!$passed -and !$p.HasExited -and $e) {
        try {
            Command 'dx12.validation' | ConvertTo-Json -Depth 10 | Set-Content "$out/validation.json"
        } catch {
            $_ | Out-String | Set-Content "$out/validation-query-error.txt"
        }
    }
    foreach($name in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}
    $p.Refresh()
    if(!$p.HasExited -and $e) {
        try {Command 'quit' | Out-Null}
        catch {$_ | Out-String | Set-Content "$out/shutdown-command-error.txt"}
    }
    $stopped=$p.WaitForExit(60000)
    $exitCode=if($stopped){$p.ExitCode}else{$null}
    @{passed=($passed -and $stopped -and $exitCode -eq 0);configuration=$Configuration;exitCode=$exitCode;processStopped=$stopped;binarySha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash} | ConvertTo-Json | Set-Content "$out/result.json"
    if(!$stopped){throw 'Normal shutdown timed out'}
}
