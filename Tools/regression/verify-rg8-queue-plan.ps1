#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$expectedChecks = 119
$expectedLog = 'RG8_QUEUE_SCHEDULE_OK schema=3 checks=119 scope=compiled-plan nativeExecution=false overlap=true declarationOrder=true readRead=true sharedGraphicsReads=true readerEpochJoin=true sideEffects=true fallbackReasons=true modelGuards=true timingIdentity=true measurementDomains=true' + "`n"
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a fresh evidence directory.'
}
New-Item -ItemType Directory -Path $out | Out-Null
@('dx12.rendergraph queue-schedule','quit') | Set-Content "$out/commands.txt" -Encoding utf8
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
$start.Environment.Remove('CREATOR_GPU_MEMORY_SAMPLES') | Out-Null
$start.Environment.Remove('CREATOR_RENDERGRAPH_ALIASING') | Out-Null
$binary = [ordered]@{configuration=$Configuration; head=(git -C $repo rev-parse HEAD);
    exe=(Get-FileHash $exe).Hash;
    runtime=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
    acceptanceSchema=3; expectedChecks=$expectedChecks; expectedLog=$expectedLog;
    wrapper=(Get-FileHash $PSCommandPath).Hash;
    implementation=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp')).Hash;
    contract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h')).Hash;
    measurementHistory=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h')).Hash;
    tests=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RenderQueueScheduleRg8Tests.h')).Hash}
$binary | ConvertTo-Json | Set-Content "$out/binary-metadata.json" -Encoding utf8
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try
{
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    while (!$process.WaitForExit(1000))
    {
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw 'Existing Editor queue schedule probe timed out.'
        }
    }
    $results = @(Read-CommandResults "$out/results.jsonl")
    if ($process.ExitCode -ne 0 -or $results.Count -ne 2)
    {
        throw 'Queue schedule acceptance requires exactly the requested probe and quit results.'
    }
    $probe = $results[0]
    $quit = $results[1]
    if ($probe.command -cne 'dx12.rendergraph' -or $probe.status -cne 'succeeded' -or $probe.code -cne 'ok' -or
        $probe.data.passed -isnot [bool] -or $probe.data.passed -ne $true -or
        $probe.data.log -isnot [string] -or $probe.data.log -cne $expectedLog -or
        $quit.command -cne 'quit' -or $quit.status -cne 'succeeded' -or $quit.code -cne 'ok')
    {
        throw 'Queue schedule identity, exact check count, terminal ordering or result failed; rebuild the tested header.'
    }
    [ordered]@{schemaVersion=2; passed=$true; phaseComplete=$false; acceptanceSchema=3; checks=$expectedChecks;
        scope='Compiled queue plan in existing offscreen Editor'; overlapPlanTested=$true;
        declarationOrderTested=$true; readReadOwnershipTested=$true; modelGuardsTested=$true; timingIdentityTested=$true;
        sharedGraphicsReadsTested=$true; readerEpochJoinTested=$true; sideEffectsTested=$true;
        fallbackReasonsTested=$true; measurementDomainsTested=$true;
        nativeQueueExecutionTested=$false;
        binary=$binary; exitCode=$process.ExitCode} |
        ConvertTo-Json -Depth 10 | Set-Content "$out/schedule-result.json" -Encoding utf8
    "RG8_PLAN_ACCEPTANCE_OK configuration=$Configuration"
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
