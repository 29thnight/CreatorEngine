#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(4,32)][int]$Samples=8,
    [ValidateRange(16,256)][int]$WarmupFrames=16,
    [ValidateRange(32,512)][int]$NormalCandidates=64,
    [switch]$TimingOnly,
    [switch]$ValidateGpu,
    # Collection fallback only; it cannot establish a same-process comparison.
    [switch]$SeparateProcesses,
    # Explicit fixture scenes may provide real positive-overlap/fallback workloads.
    # A scene name alone is not evidence; the strict audit checks actual intervals.
    [string]$ScenePath=''
)
$ErrorActionPreference='Stop'
if($ValidateGpu -and $TimingOnly){throw 'GPU validation is correctness-only, not a timing measurement'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$ScenePath){$ScenePath="$repo/Dynamic_CPP/Assets/Scenes/TestShadow.creator"}
$ScenePath=[IO.Path]::GetFullPath($ScenePath)
if(!(Test-Path -LiteralPath $ScenePath)){throw "Missing explicit workload scene: $ScenePath"}
if(Test-Path $out){throw 'Use a new evidence directory'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close the existing Editor before measurement'}
New-Item -ItemType Directory $out | Out-Null
$exe="$repo/Bin/x64-Release/Editor/CreatorEditor.exe"
$binary=@{head=(git -C $repo rev-parse HEAD);exe=(Get-FileHash $exe).Hash;runtime=(Get-FileHash "$repo/Bin/x64-Release/Editor/CreatorEditor.runtime.dll").Hash}
$binary | ConvertTo-Json | Set-Content "$out/binary.json"
$purpose=if($ValidateGpu){'gpu-validation'}elseif($TimingOnly){'normal-timing'}else{'capture-correctness'}
$sameProcessRequested=$TimingOnly -and !$SeparateProcesses
$sameProcess=@{requested=[bool]$sameProcessRequested;supported=$false;established=$false;
    command='render.queue.mode';historyPreserved=[bool]$sameProcessRequested;
    boundary=$(if($sameProcessRequested){'render-owner-next-frame'}else{'process-initialization'});
    reason=$(if($sameProcessRequested){'Runtime command support and completed same-process matrix are not yet verified'}
        else{'Separate-process collection; same-process OFF/ON is not established'})}
$context=@{schemaVersion=2;acceptanceSchema=3;scene=$ScenePath;evidencePurpose=$purpose;
    processOrder=$(if($sameProcessRequested){'same-process-forward-reverse'}else{'separate-process-forward-reverse'});
    sameProcessOffOn=$sameProcess;
    cpuScope='view-prepare-build-compile-record-schedule-submit-retirement-enqueue';
    cpuMetric='renderOnceMilliseconds';
    cpuComponentScope='record-schedule-submit-frame-retirement-enqueue';
    cpuScopeExcludes='main-thread-scene-generation;presentation;GPU-completion-wait';
    gpuSpanScope='graph-prologue-through-epilogue';
    overlapScope='calibrated-independent-pass-interval-intersection-above-clock-error';
    normalWarmup='ordinary-dx12.live-only;complete-cost-history-checked-after-identity-join';
    captureTimingPurpose='correctness-diagnostics-only';
    performanceValidated=$false;adoptionEstablished=$false;
    performanceClaim='none';
    note='Collection alone cannot establish speedup or performance adoption; placement cost constants remain uncalibrated'}
$context | ConvertTo-Json -Depth 10 | Set-Content "$out/measurement-context.json"

function Test-NormalSnapshot($live)
{
    return ($live.gpu.clockValid -and $live.gpu.queueSpanMs -gt 0 -and
        $live.gpu.busyMs -le $live.gpu.queueSpanMs + 0.00001 -and
        $live.gpu.submission -gt 0 -and $live.display.width -gt 0 -and $live.display.height -gt 0 -and
        @($live.gpu.passes | Where-Object name -Like 'PBR.*').Count -eq 0 -and
        $live.gpu.droppedTotal -eq 0 -and $live.gpu.queryOverflowPasses -eq 0 -and
        $live.gpu.spanViolations -eq 0)
}

function Get-SubmissionIdentity($row)
{
    return "$($row.backendGeneration):$($row.frameId):$($row.viewId):$($row.submissionId)"
}

function Export-NormalSamples($telemetry,$candidates,[int]$mode,[string]$directory)
{
    $indexed=@{execution=@{};timing=@{};cpu=@{}}
    foreach($tag in @('execution','timing','cpu'))
    {
        foreach($row in $telemetry[$tag])
        {
            $key=Get-SubmissionIdentity $row
            if($indexed[$tag].ContainsKey($key)){throw "Duplicate $tag submission evidence: $key"}
            $indexed[$tag][$key]=$row
        }
    }
    $selected=@()
    $joined=@()
    $rejected=@()
    foreach($row in $candidates)
    {
        $matching=@($telemetry.timing | Where-Object {
            $_.frameId -eq $row.gpu.frame -and $_.viewId -eq $row.gpu.viewId -and
            $_.submissionId -eq $row.gpu.submission
        })
        $reason=''
        $timing=$null
        $cpu=$null
        $execution=$null
        if($matching.Count -ne 1)
        {
            $reason='missing-or-ambiguous-submission-timing'
        }
        else
        {
            $timing=$matching[0]
            $key=Get-SubmissionIdentity $timing
            $cpu=$indexed.cpu[$key]
            $execution=$indexed.execution[$key]
            if($timing.mode -ne $mode -or ($cpu -and $cpu.mode -ne $mode))
            {
                $reason='submission-outside-requested-mode'
            }
            elseif($timing.captureGeneration -ne 0 -or $timing.measurementDomain -cne 'normal')
            {
                $reason='capture-or-unknown-measurement-domain'
            }
            elseif(!$cpu -or $cpu.captureGeneration -ne 0 -or $cpu.measurementDomain -cne 'normal' -or
                $cpu.scope -cne 'record-schedule-submit-frame-retirement-enqueue' -or
                $cpu.frameScope -cne 'view-prepare-build-compile-record-schedule-submit-retirement-enqueue' -or
                ![double]::IsFinite($cpu.renderOnceMilliseconds) -or
                ![double]::IsFinite($cpu.compileMilliseconds) -or $cpu.compileMilliseconds -lt 0 -or
                ![double]::IsFinite($cpu.totalMilliseconds) -or
                ![double]::IsFinite($cpu.submissionMilliseconds) -or
                $cpu.renderOnceMilliseconds + 0.00001 -lt $cpu.compileMilliseconds + $cpu.totalMilliseconds -or
                $cpu.totalMilliseconds -lt $cpu.submissionMilliseconds -or $cpu.submissionMilliseconds -lt 0)
            {
                $reason='missing-or-invalid-full-cpu-cost'
            }
            elseif(!$timing.clockValid -or $timing.cpuTicksPerSecond -le 0 -or
                $timing.queryOverflow -ne 0 -or $timing.droppedSlices -ne 0 -or
                $timing.sliceCount -le 0 -or @($timing.slices).Count -ne $timing.sliceCount -or
                $timing.graphicsCalibrationSamples -le 0 -or
                ($timing.computeSliceCount -gt 0 -and $timing.computeCalibrationSamples -le 0))
            {
                $reason='missing-or-invalid-calibrated-intervals'
            }
            elseif($mode -eq 0 -and $execution)
            {
                $reason='off-mode-used-owned-executor'
            }
            elseif($mode -ne 0)
            {
                if(!$execution -or $execution.measurementDomain -cne 'normal' -or
                    $execution.captureGeneration -ne 0 -or $execution.requestedExecutionMode -ne $mode -or
                    !$execution.execution.completed -or $execution.execution.recoveryRequired)
                {
                    $reason='missing-or-incomplete-normal-execution'
                }
                elseif(!$execution.schedule.predictionComplete -or
                    $execution.schedule.missingMeasurementPasses -ne 0 -or
                    $execution.schedule.measuredPasses -ne @($execution.schedule.entries).Count)
                {
                    $reason='incomplete-warm-normal-cost-history'
                }
                elseif($execution.fallbackReason -cne $execution.schedule.fallbackReason -or
                    $execution.fallbackReason -cnotin @('none','disabled','unsupported','unsupported-state','insufficient-gain') -or
                    ($execution.execution.computeBatches -gt 0 -and $execution.fallbackReason -cne 'none') -or
                    ($mode -eq 2 -and $execution.execution.computeBatches -eq 0 -and $execution.fallbackReason -ceq 'none'))
                {
                    $reason='missing-or-inconsistent-fallback-reason'
                }
            }
        }
        if($reason)
        {
            $rejected+=@{frameId=$row.gpu.frame;viewId=$row.gpu.viewId;submissionId=$row.gpu.submission;reason=$reason}
        }
        elseif($selected.Count -lt 32)
        {
            # Selection is chronological and never depends on a fast duration or
            # a positive overlap outcome. The strict audit recomputes intersections.
            $selected+=$row
            $joined+=@{identity=(Get-SubmissionIdentity $timing);measurementDomain='normal';
                fullCostHistory=$(if($mode -eq 0){'not-applicable-off'}else{'complete'});
                fullCpuCostMs=$cpu.renderOnceMilliseconds;recordScheduleSubmitMs=$cpu.totalMilliseconds;
                cpu=$cpu;timing=$timing;execution=$execution}
        }
    }
    @{schemaVersion=2;acceptanceSchema=3;evidencePurpose='normal-timing';
        candidateCount=$candidates.Count;selectedCount=$selected.Count;requiredCount=32;
        selection='first-32-eligible-after-ordinary-warmup';rejected=$rejected;
        cpuMetric='renderOnceMilliseconds';
        calibratedIntervalsRequireAudit=$true;performanceValidated=$false;adoptionEstablished=$false} |
        ConvertTo-Json -Depth 20 | Set-Content "$directory/normal-selection.json"
    ConvertTo-Json -InputObject @($selected) -Depth 70 | Set-Content "$directory/normal-frames.json"
    ConvertTo-Json -InputObject @($joined) -Depth 70 | Set-Content "$directory/normal-samples.json"
    if($selected.Count -ne 32)
    {
        $script:hasIncompleteNormalSamples=$true
        Write-Warning "Insufficient warm ordinary frames with full cost and calibrated interval evidence: $($selected.Count)/32; see normal-selection.json. Continue collecting diagnostics; final acceptance will fail."
    }
}
$runs=@()
$script:hasIncompleteNormalSamples=$false
$orders=if($ValidateGpu){@('Forward')}else{@('Forward','Reverse')}
foreach($order in $orders)
{
    $modes=if($ValidateGpu){@(1,2)}elseif($order -eq 'Forward'){@(0,1,2)}else{@(2,1,0)}
    $groups=if($sameProcessRequested){@(@{modes=$modes;name="$order-process"})}
        else{@($modes | ForEach-Object { @{modes=@($_);name="$order-$_"} })}
    foreach($group in $groups)
    {
        $processCase="$out/$($group.name)"
        $case=$processCase
        $pendingRuns=@()
        New-Item -ItemType Directory "$processCase/workspace" -Force | Out-Null
        Copy-Item "$repo/Dynamic_CPP/Saved/Editor/Editor/Workspaces/active.workspace" "$processCase/workspace/active.workspace"
        $start=[Diagnostics.ProcessStartInfo]::new($exe)
        $start.WorkingDirectory=Split-Path $exe
        $start.UseShellExecute=$false
        $start.CreateNoWindow=$true
        $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $start.RedirectStandardOutput=$true
        $start.RedirectStandardError=$true
        foreach($arg in @('--development-project',"$repo/Dynamic_CPP",'--command-service'))
        {
            $start.ArgumentList.Add($arg)
        }
        $start.Environment['CREATOR_DX12_VALIDATION']=if($ValidateGpu){'gpu'}else{'off'}
        $start.Environment['CREATOR_RENDERGRAPH_QUEUE_EXECUTION']="$($group.modes[0])"
        $start.Environment['CREATOR_RG8_EVIDENCE']='1'
        $start.Environment['CREATOR_RENDERGRAPH_ALIASING']='0'
        $start.Environment['CREATOR_RENDERGRAPH_EXTEND_LIFETIMES']='0'
        $start.Environment['CREATOR_EDITOR_WORKSPACE_DIR']="$processCase/workspace"
        $start.Environment['CREATOR_EDITOR_LEGACY_INI']="$processCase/workspace/legacy.ini"
        $start.Environment['CREATOR_GPU_MEMORY_SAMPLES']="$processCase/memory-continuous.jsonl"
        $process=[Diagnostics.Process]::Start($start)
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        try
        {
            $deadline=[DateTime]::UtcNow.AddSeconds(180)
            do
            {
                if($process.HasExited){throw 'Editor exited during startup'}
                $endpoint=$null
                if(Test-Path "$repo/Dynamic_CPP/Library/CommandService/endpoint.json")
                {
                    $endpoint=Get-Content "$repo/Dynamic_CPP/Library/CommandService/endpoint.json" -Raw | ConvertFrom-Json
                }
                if([DateTime]::UtcNow -gt $deadline){throw 'Endpoint timeout'}
                Start-Sleep -Milliseconds 100
            }until($endpoint -and $endpoint.pid -eq $process.Id)
            $base="http://127.0.0.1:$($endpoint.port)"
            $headers=@{Authorization="Bearer $($endpoint.token)"}
            function Cmd([string]$name,[string[]]$arguments=@(),[bool]$allowPreparing=$false)
            {
                @{command=$name;utcMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()} | ConvertTo-Json -Compress | Add-Content "$case/stages.jsonl"
                $body=@{command=$name;args=@($arguments);mode='async'} | ConvertTo-Json -Compress
                $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 20
                if($name -ne 'quit' -and $result.operationId)
                {
                    $poll=$result.poll;
                    # GPU validation can compile instrumented driver shaders on
                    # first use. Keep timing runs on their original short bound.
                    $until=[DateTime]::UtcNow.AddSeconds($(if($ValidateGpu){600}else{180}))
                    do
                    {
                        Start-Sleep -Milliseconds 100
                        $result=Invoke-RestMethod "$base$poll" -Headers $headers -TimeoutSec 20
                        if([DateTime]::UtcNow -gt $until){throw "$name timeout"}
                    }until($result.state -eq 'completed')
                }
                @{command=$name;arguments=$arguments;result=$result} | ConvertTo-Json -Depth 70 -Compress | Add-Content "$case/commands.jsonl"
                if($name -eq 'quit'){return $result}
                if($allowPreparing -and $result.message -eq 'LX Scene requested material program is still preparing.'){return $null}
                if($result.status -ne 'succeeded'){throw "$name failed: $($result.message)"}
                return $result.data
            }
            $load=Cmd 'scene.switch' @($ScenePath)
            $deadline=[DateTime]::UtcNow.AddSeconds(180)
            do
            {
                $state=Cmd 'scene.load.status' @("$($load.requestId)")
                if([DateTime]::UtcNow -gt $deadline){throw 'Scene load timeout'}
                Start-Sleep -Milliseconds 100
            }until($state.complete)
            Cmd 'editor.viewport' @('scene') | Out-Null
            Cmd 'editor.renderscale' @('auto') | Out-Null
            if($TimingOnly)
            {
                # Confirm ordinary rendering is live before requesting a mode
                # boundary. This readiness check never creates a capture domain.
                $ready=Cmd 'dx12.live' @('on')
                $deadline=[DateTime]::UtcNow.AddSeconds(180)
                while(!$ready.ready -or !(Test-NormalSnapshot $ready))
                {
                    if([DateTime]::UtcNow -gt $deadline){throw 'Ordinary DX12 rendering did not become ready'}
                    Start-Sleep -Milliseconds 100
                    $ready=Cmd 'dx12.live'
                }
            }
            foreach($mode in $group.modes)
            {
                $case="$out/$order-$mode"
                New-Item -ItemType Directory $case -Force | Out-Null
                $appliedFrame=0L
                if($sameProcessRequested)
                {
                    try
                    {
                        $switchResult=Cmd 'render.queue.mode' @("$mode")
                        if($switchResult.requestedMode -ne $mode -or $switchResult.appliedMode -ne $mode -or
                            $switchResult.requestId -le 0 -or $switchResult.appliedFrame -le 0)
                        {
                            throw 'Runtime queue-mode command did not confirm the requested render-owner boundary'
                        }
                        $appliedFrame=[long]$switchResult.appliedFrame
                        $switchResult | ConvertTo-Json -Depth 10 | Set-Content "$case/mode-switch.json"
                        $sameProcess.supported=$true
                        $sameProcess.reason='Runtime command verified; collection and submission identity audit are still pending'
                        $context | ConvertTo-Json -Depth 10 | Set-Content "$out/measurement-context.json"
                    }
                    catch
                    {
                        $sameProcess.established=$false
                        $sameProcess.reason="Runtime OFF/ON switching unavailable or failed: $($_.Exception.Message). Separate-process collection remains available with -SeparateProcesses."
                        $context | ConvertTo-Json -Depth 10 | Set-Content "$out/measurement-context.json"
                        throw
                    }
                }
                @{schemaVersion=2;acceptanceSchema=3;evidencePurpose=$purpose;sameProcess=[bool]$sameProcessRequested;
                    processId=$process.Id;processPath=$processCase;order=$order;mode=$mode;appliedFrame=$appliedFrame} |
                    ConvertTo-Json -Depth 10 | Set-Content "$case/run-context.json"
                if($TimingOnly)
                {
                    # Capture readbacks, capture-domain costs and output comparison
                    # never participate in the normal timing process, including warmup.
                    $warm=@()
                    $candidates=@()
                    $seen=@{}
                    $deadline=[DateTime]::UtcNow.AddSeconds(180)
                    while($warm.Count -lt $WarmupFrames -or $candidates.Count -lt $NormalCandidates)
                    {
                        $live=Cmd 'dx12.live'
                        if([DateTime]::UtcNow -gt $deadline){throw 'Insufficient ordinary warmup/candidate frames'}
                        $identity="$($live.gpu.frame):$($live.gpu.viewId):$($live.gpu.submission)"
                        if((Test-NormalSnapshot $live) -and $live.gpu.frame -gt $appliedFrame -and !$seen.ContainsKey($identity))
                        {
                            $seen[$identity]=$true
                            $sample=@{utcMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();gpu=$live.gpu;display=$live.display}
                            if($warm.Count -lt $WarmupFrames)
                            {
                                $warm+=$sample
                                ConvertTo-Json -InputObject @($warm) -Depth 70 | Set-Content "$case/warmup-frames.json"
                            }
                            elseif($live.gpu.viewId -eq $warm[-1].gpu.viewId -and
                                $live.display.width -eq $warm[-1].display.width -and $live.display.height -eq $warm[-1].display.height)
                            {
                                $candidates+=$sample
                                ConvertTo-Json -InputObject @($candidates) -Depth 70 | Set-Content "$case/normal-candidates.json"
                            }
                        }
                        Start-Sleep -Milliseconds 100
                    }
                    Write-Output "RG8_NORMAL_CANDIDATES order=$order mode=$mode frames=$($candidates.Count)"
                }
                else
                {
                    $attempt=0
                    do
                    {
                        $warm=Cmd 'render.live.capture' @("$case/warm-$attempt",'editor','controlled') $true
                        ++$attempt
                        if($attempt -gt 40){throw 'Warm preparation timeout'}
                        Start-Sleep -Milliseconds 250
                    }until($warm)
                    Start-Sleep -Seconds 2
                }
                $captureSamples=if($TimingOnly){0}else{$Samples}
                for($index=0;$index -lt $captureSamples;++$index)
                {
                    Cmd 'render.live.capture' @("$case/sample-$index",'editor','controlled') | Out-Null
                    Write-Output "RG8_SAMPLE order=$order mode=$mode sample=$index"
                    Start-Sleep -Milliseconds 250
                }
                Cmd 'dx12.live' | ConvertTo-Json -Depth 70 | Set-Content "$case/live-status.json"
                if($ValidateGpu)
                {
                    Cmd 'render.live.fence' @('600') | Out-Null
                    $validation=Cmd 'dx12.validation'
                    $validation | ConvertTo-Json -Depth 30 | Set-Content "$case/validation.json"
                    if(!$validation.layerEnabled -or $validation.mode -ne 'gpu' -or
                        $validation.problems -ne 0 -or $validation.droppedMessages -ne 0)
                    {
                        throw 'GPU validation is missing or reported a problem'
                    }
                }
                $pendingRuns+=@{schemaVersion=2;acceptanceSchema=3;scene=$ScenePath;order=$order;mode=$mode;path=$case;
                    processId=$process.Id;evidencePurpose=$purpose;samples=$captureSamples;timingOnly=[bool]$TimingOnly;
                    sameProcess=[bool]$sameProcessRequested;processPath=$processCase;appliedFrame=$appliedFrame;
                    warmNormalOnly=[bool]$TimingOnly;gpuValidation=[bool]$ValidateGpu;exitCode=$null}
            }
            $case=$processCase
            Cmd 'quit' | Out-Null
            if(!$process.WaitForExit(30000)){throw 'Editor shutdown timeout'}
            if($process.ExitCode -ne 0){throw "Editor exit $($process.ExitCode)"}
            foreach($run in $pendingRuns){$run.exitCode=$process.ExitCode}
        }
        finally
        {
            if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
            $stdout.GetAwaiter().GetResult() | Set-Content "$processCase/stdout.log"
            $stderr.GetAwaiter().GetResult() | Set-Content "$processCase/stderr.log"
            $process.Dispose()
        }
        # Preserve the complete actual plan and raw calibrated slices, not just
        # the last dx12.live sample. Joining uses backend/view/submission identity.
        $telemetry=@{schemaVersion=2;sameProcess=[bool]$sameProcessRequested;execution=@();timing=@();cpu=@()}
        foreach($line in (Get-Content "$processCase/stdout.log"))
        {
            if($line -match '\[rg8\.(execution|timing|cpu)\] (\{.*\})')
            {
                $tag=$Matches[1]
                $telemetry[$tag]+=($Matches[2] | ConvertFrom-Json)
            }
        }
        if((Get-FileHash $exe).Hash -ne $binary.exe -or (Get-FileHash "$repo/Bin/x64-Release/Editor/CreatorEditor.runtime.dll").Hash -ne $binary.runtime)
        {
            throw 'Binary changed during measurement'
        }
        foreach($run in $pendingRuns)
        {
            $case=$run.path
            $mode=$run.mode
            if($telemetry.timing.Count -eq 0 -or $telemetry.cpu.Count -eq 0 -or
                ($mode -ne 0 -and @($telemetry.execution | Where-Object mode -EQ $mode).Count -eq 0))
            {
                throw 'Current-binary per-submission telemetry is missing'
            }
            if($case -ne $processCase)
            {
                # Full process logs retain transitions, late retirements and
                # aggregate counters. The audit filters mode only after joins.
                foreach($file in @('stdout.log','stderr.log','memory-continuous.jsonl'))
                {
                    Copy-Item "$processCase/$file" "$case/$file"
                }
            }
            $telemetry | ConvertTo-Json -Depth 70 | Set-Content "$case/queue-evidence.json"
            if($TimingOnly)
            {
                $candidates=@(Get-Content "$case/normal-candidates.json" -Raw | ConvertFrom-Json)
                Export-NormalSamples $telemetry $candidates $mode $case
                Write-Output "RG8_NORMAL_COLLECTED order=$order mode=$mode domain=normal; see normal-selection.json for accepted count"
            }
            $runs+=$run
            ConvertTo-Json -InputObject @($runs) -Depth 10 | Set-Content "$out/runs.json"
        }
    }
}
if($sameProcessRequested)
{
    $sameProcess.established=!$script:hasIncompleteNormalSamples
    $sameProcess.reason=if($script:hasIncompleteNormalSamples){'Both mode orders collected, but required valid normal samples are incomplete; diagnostics only'}else{'Both mode orders collected within their original Editor process; full acceptance and calibrated model validation remain required'}
    $context | ConvertTo-Json -Depth 10 | Set-Content "$out/measurement-context.json"
}
if($script:hasIncompleteNormalSamples){throw 'RG8 normal collection retained all mode diagnostics, but required valid samples are incomplete; see normal-selection.json'}
'RG8_MEASUREMENT_COMPLETE'
