#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$OwnershipOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a fresh evidence directory.'
}
New-Item -ItemType Directory -Path $out | Out-Null
$commands = if ($OwnershipOnly) { @('dx12.rendergraph queue-ownership','quit') } else { @('dx12.rendergraph queue-contract','dx12.rendergraph queue-native','dx12.rendergraph queue-ownership','dx12.rendergraph','quit') }
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
$start.Environment['CREATOR_DX12_VALIDATION'] = 'gpu'
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR'] = Join-Path $out 'workspace'
$start.Environment['CREATOR_EDITOR_LEGACY_INI'] = Join-Path $out 'workspace/legacy.ini'
$start.Environment.Remove('CREATOR_GPU_MEMORY_SAMPLES') | Out-Null
$start.Environment.Remove('CREATOR_RENDERGRAPH_ALIASING') | Out-Null
$binary = [ordered]@{configuration=$Configuration; offscreen=$true; validation='gpu'; head=(git -C $repo rev-parse HEAD);
    exe=(Get-FileHash $exe).Hash;
    runtime=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
    contract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/RHIQueueContract.h')).Hash;
    provider=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12QueueService.cpp')).Hash;
    adapter=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/DX12/DX12QueueBatchAdapter.h')).Hash;
    tests=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RHIQueueOwnershipTests.h')).Hash}
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
            throw 'Existing Editor native queue/regression probe timed out.'
        }
    }
    $results = @(Read-CommandResults "$out/results.jsonl")
    $probe = @($results | Where-Object { $_.command -eq 'dx12.rendergraph' -and $_.data.log -match 'Q0_QUEUE_OWNERSHIP_' })
    if ($process.ExitCode -ne 0 -or $results.Count -ne $(if ($OwnershipOnly) {2} else {5}) -or $probe.Count -ne 1 -or
        @($results | Where-Object status -ne 'succeeded').Count -or
        !$probe[0].data.passed -or
        $probe[0].data.log -notmatch 'Q0_QUEUE_OWNERSHIP_OK checks=43 bytes=16384 payloadError=0 validationErrors=0 delayedProducer=true quarantineReleased=true')
    {
        throw 'Native queue/regression acceptance failed or tested header is stale.'
    }
    [ordered]@{passed=$true; probeOnly=$OwnershipOnly; fixedQ0ChecksPassed=(!$OwnershipOnly); checks=[int]([regex]::Match($probe[0].data.log, 'checks=(\d+)').Groups[1].Value); scope='Queue submission and delayed payload in existing Editor';
        fullRenderGraphRegressionPassed=(!$OwnershipOnly); nativeQueuePrimitivesTested=$true; payloadReadbackTested=$true; binary=$binary; exitCode=$process.ExitCode} |
        ConvertTo-Json -Depth 10 | Set-Content "$out/ownership-result.json" -Encoding utf8
    "Q0_OWNERSHIP_ACCEPTANCE_OK configuration=$Configuration"
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
