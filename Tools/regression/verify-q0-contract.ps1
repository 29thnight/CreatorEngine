#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory
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
@('dx12.rendergraph queue-contract','quit') | Set-Content "$out/commands.txt" -Encoding utf8
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = Split-Path $exe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($arg in @('--commandlet-script',"$out/commands.txt",'--result-format','jsonl','--result-file',"$out/results.jsonl"))
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
    contract=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/RHIQueueContract.h')).Hash;
    tests=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RHIQueueContractTests.h')).Hash}
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
            throw 'Existing Editor queue contract probe timed out.'
        }
    }
    $results = @(Read-CommandResults "$out/results.jsonl")
    $probe = @($results | Where-Object command -eq 'dx12.rendergraph')
    if ($process.ExitCode -ne 0 -or $results.Count -ne 2 -or $probe.Count -ne 1 -or
        @($results | Where-Object status -ne 'succeeded').Count -or
        !$probe[0].data.passed -or
        $probe[0].data.log -notmatch 'Q0_QUEUE_CONTRACT_OK checks=23 scope=cpu-contract nativeQueues=not-implemented')
    {
        throw 'Queue contract acceptance failed or tested header is stale.'
    }
    [ordered]@{passed=$true; phaseComplete=$false; checks=23; scope='CPU contract in existing Editor';
        nativeQueueExecutionTested=$false; binary=$binary; exitCode=$process.ExitCode} |
        ConvertTo-Json -Depth 10 | Set-Content "$out/contract-result.json" -Encoding utf8
    "Q0_CONTRACT_ACCEPTANCE_OK configuration=$Configuration"
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
