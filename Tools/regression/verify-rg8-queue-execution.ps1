#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$FullRegression,
    # Opt-in hardware acceptance. Generic correctness must still run on hardware
    # or GPU-validation configurations that serialize otherwise eligible queues.
    [switch]$RequireMeasuredOverlap
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$expectedChecks = 434
$expectedLog = 'RG8_QUEUE_EXECUTION_OK schema=3 checks=434 payloadError=0 validationErrors=0 modes=2 overlapCases=7 positiveOverlapExecution=true negativeFallbackExecution=true declarationOrder=true readRead=true reorderedFailure=true delayedLifetime=true quarantineReleased=true frameRetirement=true parallelRecording=true calibratedIntervals=true measuredGain=false' + "`n"
$expectedScheduleChecks = 119
$expectedScheduleLog = 'RG8_QUEUE_SCHEDULE_OK schema=3 checks=119 scope=compiled-plan nativeExecution=false overlap=true declarationOrder=true readRead=true sharedGraphicsReads=true readerEpochJoin=true sideEffects=true fallbackReasons=true modelGuards=true timingIdentity=true measurementDomains=true' + "`n"
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a fresh evidence directory.'
}
New-Item -ItemType Directory -Path $out | Out-Null
$commands = @('dx12.rendergraph queue-execution')
if ($FullRegression)
{
    $commands += @('dx12.rendergraph queue-schedule','dx12.rendergraph')
}
$commands += 'quit'
$commands | Set-Content "$out/commands.txt" -Encoding utf8
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = Split-Path $exe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($arg in @('--smoke-offscreen','--commandlet-script',"$out/commands.txt",'--result-format','jsonl','--result-file',"$out/results.jsonl"))
{
    $start.ArgumentList.Add($arg)
}
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR'] = Join-Path $out 'workspace'
$start.Environment['CREATOR_EDITOR_LEGACY_INI'] = Join-Path $out 'workspace/legacy.ini'
$start.Environment['CREATOR_DX12_VALIDATION'] = 'gpu'
$start.Environment.Remove('CREATOR_GPU_MEMORY_SAMPLES') | Out-Null
$start.Environment.Remove('CREATOR_RENDERGRAPH_ALIASING') | Out-Null
$binary = [ordered]@{configuration=$Configuration; head=(git -C $repo rev-parse HEAD);
    exe=(Get-FileHash $exe).Hash;
    runtime=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
    acceptanceSchema=3; expectedChecks=$expectedChecks; expectedLog=$expectedLog;
    expectedScheduleChecks=$expectedScheduleChecks; expectedScheduleLog=$expectedScheduleLog;
    wrapper=(Get-FileHash $PSCommandPath).Hash;
    scheduleWrapper=(Get-FileHash (Join-Path $PSScriptRoot 'verify-rg8-queue-plan.ps1')).Hash;
    scheduleTests=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RenderQueueScheduleRg8Tests.h')).Hash;
    timingContract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/IRHIGpuProfiler.h')).Hash;
    measurementHistory=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h')).Hash;
    profiler=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12GpuProfiler.cpp')).Hash;
    recorderContract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/RHIQueueRecorder.h')).Hash;
    recorderHeader=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12QueueRecorder.h')).Hash;
    executor=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp')).Hash;
    provider=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp')).Hash;
    frameResources=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp')).Hash;
    frameContract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12DeviceResources.h')).Hash;
    liveAdapter=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/EnhancedSceneRendererLiveDX12Adapter.cpp')).Hash;
    sceneContract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h')).Hash;
    captureContract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h')).Hash;
    computeAuthor=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSAOPass.cpp')).Hash;
    liveScene=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp')).Hash;
    encoder=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12Encoder.cpp')).Hash;
    shader=(Get-FileHash (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/SelfTest/QueueRg8.slang')).Hash;
    implementation=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp')).Hash;
    contract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h')).Hash;
    tests=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RenderQueueExecutionRg8Tests.h')).Hash}
$binary | ConvertTo-Json | Set-Content "$out/binary-metadata.json" -Encoding utf8
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try
{
    $deadline = [DateTime]::UtcNow.AddSeconds(600)
    while (!$process.WaitForExit(1000))
    {
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw 'Existing Editor queue execution probe timed out.'
        }
    }
    $results = @(Read-CommandResults "$out/results.jsonl")
    if ($process.ExitCode -ne 0 -or $results.Count -ne $commands.Count)
    {
        throw 'Queue execution acceptance requires exactly the requested terminal results and exit code zero.'
    }
    for ($index = 0; $index -lt $results.Count; ++$index)
    {
        $expectedCommand = if ($index -eq $results.Count - 1) { 'quit' } else { 'dx12.rendergraph' }
        if ($results[$index].command -cne $expectedCommand -or $results[$index].status -cne 'succeeded' -or
            $results[$index].code -cne 'ok')
        {
            throw "Unexpected command/status/code at terminal result index $index."
        }
        if ($expectedCommand -eq 'dx12.rendergraph' -and
            ($results[$index].data.passed -isnot [bool] -or $results[$index].data.passed -ne $true -or
             $results[$index].data.log -isnot [string]))
        {
            throw "Missing typed render-test result at terminal result index $index."
        }
    }
    $probe = $results[0]
    if (!$probe.data.log.StartsWith($expectedLog, [StringComparison]::Ordinal))
    {
        throw 'Queue execution suite identity or exact check count differs; rebuild the tested header.'
    }
    $intervalText = $probe.data.log.Substring($expectedLog.Length)
    $intervalMatch = [regex]::Match($intervalText, '\ARG8_QUEUE_INTERVALS (\{[^\r\n]*\})\n\z')
    if (!$intervalMatch.Success)
    {
        throw 'Missing or duplicate calibrated native interval evidence.'
    }
    $intervals = $intervalMatch.Groups[1].Value | ConvertFrom-Json
    if ($intervals.schemaVersion -ne 1 -or $intervals.scope -cne 'native-correctness' -or
        @($intervals.cases).Count -ne 4 -or
        (@($intervals.cases.case | Sort-Object -Unique) -join ',') -cne '0,1,2,3')
    {
        throw 'Native interval evidence must cover the exact positive/reference/fallback matrix.'
    }
    $measuredOverlap = $false
    foreach ($case in $intervals.cases)
    {
        if ($case.cpuTicksPerSecond -le 0 -or $case.graphicsCalibrationSamples -lt 2 -or
            $case.computeCalibrationSamples -lt 2 -or $case.clockErrorCpuTicks -lt 4 -or
            $case.usesCompute -ne ($case.case -eq 1))
        {
            throw 'Queue clocks or selected native case do not match the calibrated evidence.'
        }
        $overlap = [decimal]0
        $previousEnd = [decimal]0
        foreach ($slice in $case.computeIntervals)
        {
            $begin = [decimal]$slice.beginCpuTick
            $end = [decimal]$slice.endCpuTick
            if ($begin -le 0 -or $end -lt $begin -or $begin -lt $previousEnd)
            {
                throw 'Native compute intervals are invalid or overlap their own queue.'
            }
            $previousEnd = $end
            $intersectionBegin = [Math]::Max([decimal]$case.shadowBeginCpuTick, $begin)
            $intersectionEnd = [Math]::Min([decimal]$case.shadowEndCpuTick, $end)
            $overlap += [Math]::Max([decimal]0, $intersectionEnd - $intersectionBegin)
        }
        if ($overlap -ne [decimal]$case.overlapCpuTicks -or
            ($case.usesCompute -and (@($case.computeIntervals).Count -ne 2 -or
                $case.shadowEndCpuTick -le $case.shadowBeginCpuTick)) -or
            (!$case.usesCompute -and (@($case.computeIntervals).Count -ne 0 -or $overlap -ne 0)))
        {
            throw 'Reported overlap does not equal independently calibrated interval intersections.'
        }
        foreach ($name in @('scheduleMilliseconds','recordingMilliseconds','submissionMilliseconds','totalMilliseconds'))
        {
            $value = [double]$case.$name
            if (![double]::IsFinite($value) -or $value -lt 0)
            {
                throw 'Missing/invalid full native CPU cost evidence.'
            }
        }
        if ($case.totalMilliseconds + 0.00001 -lt
            $case.scheduleMilliseconds + $case.recordingMilliseconds + $case.submissionMilliseconds)
        {
            throw 'Native CPU total omits a scheduling/recording/submission phase.'
        }
        if ($case.usesCompute -and $overlap -gt [decimal]$case.clockErrorCpuTicks)
        {
            $measuredOverlap = $true
        }
    }
    $intervals | ConvertTo-Json -Depth 15 | Set-Content "$out/native-intervals.json" -Encoding utf8
    if ($RequireMeasuredOverlap -and !$measuredOverlap)
    {
        throw 'No native GPU overlap above calibrated clock uncertainty; correctness is not overlap proof.'
    }
    if ($FullRegression)
    {
        if ($results[1].data.log -cne $expectedScheduleLog)
        {
            throw 'Full regression queue schedule identity or exact check count differs.'
        }
        # The ordinary render-graph suite has additional diagnostic lines. Pin its
        # distinct fixed success identities once each, rather than accepting any
        # successful dx12.rendergraph row or another queue-test result.
        $regressionLines = $results[2].data.log.Split("`n")
        $requiredLines = @(
            'RG1_DAG_OK shuffles=24 stable RAW UAV-read cycle missing multiwriter Modify invalid duplicate access-state',
            'RG2_DAG_OK shuffles=240 texture buffer RAW WAR WAW Modify fork stale kind missing import cycle',
            'RG3_PLAN_OK shuffles=120 dead-version WAR-bridge sorted-lifetime transitions UAV-read-write import-final Modify stale',
            'RG4_WAVES_OK shuffles=24 critical-path culled',
            'RG5_PRODUCERS_OK passes=GBuffer/Shadow modes=3 orders=2 outputs=7 RAW=7',
            'RG5_CONSUMERS_OK Deferred/SkyBox policies=2 paths=2 Modify/WAR/depth',
            'RG5_INDIRECT_OK SSAO/SSGI policies=2 optional=2 frames=2 history-v1',
            'RG5_FORWARD_OK Code policies=2 paths=2 batches=3 tile-v1 color-v3',
            'RG5_LOOKUP_OK policies=2 capture=11 iterations=2 outputs-v2 WAR range-rejection',
            'RG5_SPECIAL_OK policies=2 Refraction/SSS-v2 Volume-coeff-v1/output-v0 WAR duplicate-rejection',
            'RG5_SURFACE_OK policies=2 iterations=3 depth-v5 color-v9 dedup WAR writable/state-rejection',
            'RG5_SPRITE_GPU_OK policies=3 frames=18 maxError=0 shared-source depth empty-output',
            'RG5_SCREEN_GPU_OK policies=3 frames=48 maxError=0 SSS/SSR enabled bypass mask missing sealed-inputs',
            'RG5_FINAL_GPU_OK policies=3 frames=129 stages=9 maxError=0 Fog/PostChain/UI/Grid/Wire/Icon/Line capture-present history-reset missing owned-inputs',
            'RG4_GPU_OK immediate workers=1/2/4 fallback split join compiled-order pixels=0 drops=0'
        )
        foreach ($line in $requiredLines)
        {
            if (@($regressionLines | Where-Object { $_ -ceq $line }).Count -ne 1)
            {
                throw "Full regression identity missing or duplicated: $line"
            }
        }
    }
    [ordered]@{schemaVersion=2; passed=$true; phaseComplete=$false; acceptanceSchema=3; checks=$expectedChecks;
        scope='Native queue execution in existing offscreen Editor'; fullRegression=[bool]$FullRegression;
        scheduleChecks=$(if ($FullRegression) { $expectedScheduleChecks } else { 0 });
        nativeQueueExecutionTested=$true; frameRetirementTested=$true; positiveOverlapExecutionTested=$true;
        negativeFallbackExecutionTested=$true; declarationOrderTested=$true; readReadOwnershipTested=$true;
        reorderedFailureTested=$true; parallelRecordingTested=$true; calibratedIntervalsTested=$true;
        measuredOverlapRequired=[bool]$RequireMeasuredOverlap; measuredOverlapEstablished=$measuredOverlap;
        intervalEvidence="$out/native-intervals.json"; performanceValidated=$false; adoptionEstablished=$false;
        sharedGraphicsReadsTested=[bool]$FullRegression; readerEpochJoinTested=[bool]$FullRegression;
        sideEffectsTested=[bool]$FullRegression; fallbackReasonsTested=[bool]$FullRegression;
        measurementDomainsTested=[bool]$FullRegression;
        liveSceneQueueCutover=$false; binary=$binary; exitCode=$process.ExitCode} |
        ConvertTo-Json -Depth 10 | Set-Content "$out/execution-result.json" -Encoding utf8
    "RG8_EXECUTION_ACCEPTANCE_OK configuration=$Configuration"
}
finally
{
    if (!$process.HasExited)
    {
        $process.Kill()
        $process.WaitForExit(15000) | Out-Null
    }
    [IO.File]::WriteAllText("$out/stdout.log",$stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText("$out/stderr.log",$stderr.GetAwaiter().GetResult())
}
