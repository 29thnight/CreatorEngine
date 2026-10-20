#Requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a fresh software RHI native evidence directory.'
}
New-Item -ItemType Directory -Path $out | Out-Null
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
@('dx12.rendergraph', 'quit') | Set-Content "$out/commands.txt" -Encoding utf8
$exe = Join-Path $repo 'Bin/x64-Debug/Editor/CreatorEditor.exe'
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = Split-Path $exe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($arg in @('--smoke-offscreen', '--commandlet-script', "$out/commands.txt", '--result-format', 'jsonl', '--result-file', "$out/results.jsonl"))
{
    $start.ArgumentList.Add($arg)
}
$start.Environment['CREATOR_SOFTWARE_RHI_PROBE'] = '1'
$start.Environment['CREATOR_DX12_VALIDATION'] = 'gpu'
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR'] = "$out/workspace"
$start.Environment['CREATOR_EDITOR_LEGACY_INI'] = "$out/workspace/legacy.ini"
@{head=(git -C $repo rev-parse HEAD);configuration='Debug';validation='gpu';
    runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
    testsSha256=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RHISoftwareNativeTests.h')).Hash;
    translatorSha256=(Get-FileHash (Join-Path $repo 'Engine/RenderEngine/RHI/RHICommandTranslator.cpp')).Hash} |
    ConvertTo-Json | Set-Content "$out/identity.json"
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try
{
    $deadline = [DateTime]::UtcNow.AddSeconds(600)
    while (!$process.WaitForExit(1000))
    {
        if ([DateTime]::UtcNow -gt $deadline)
        {
            throw 'Software RHI native regression timed out.'
        }
    }
    $stdout.GetAwaiter().GetResult() | Set-Content "$out/stdout.log"
    $stderr.GetAwaiter().GetResult() | Set-Content "$out/stderr.log"
    $rows = @(Read-CommandResults "$out/results.jsonl")
    $graph = Get-SucceededCommand $rows 'dx12.rendergraph'
    $quit = Get-SucceededCommand $rows 'quit'
    if ($process.ExitCode -ne 0 -or $rows.Count -ne 2 -or $graph.passed -ne $true -or
        $graph.log -notmatch 'RHI_SOFTWARE_NATIVE_OK commands=4 pixels=64 channels=4 staleRejected=true asynchronous=true frozenTargets=true uploadRollover=true poolReuse=true arenaRetirement=true gpuMergeReuse=true declarationMetadata=true parallelTranslation=true declarationPackets=true retainedBlackboard=true singleCopyPassUsages=true validationErrors=0')
    {
        throw 'Software RHI native output or full RenderGraph regression failed.'
    }
    @{passed=$true;normalExit=$true;nativeClearBarrierReadback=$true;fullRenderGraph=$true;
        validation='gpu';asynchronousTranslation=$true;frozenTargets=$true;uploadRollover=$true;translationPoolReuse=$true;arenaRetirement=$true;gpuMergeReuse=$true;declarationMetadata=$true;parallelTranslation=$true;declarationPackets=$true;retainedBlackboard=$true;singleCopyPassUsages=$true;creatorRobotSoftwarePath=$false} | ConvertTo-Json | Set-Content "$out/result.json"
    $graph.log
}
finally
{
    if (!$process.HasExited)
    {
        $process.Kill($true)
        $process.WaitForExit()
        $stdout.GetAwaiter().GetResult() | Set-Content "$out/stdout.log"
        $stderr.GetAwaiter().GetResult() | Set-Content "$out/stderr.log"
    }
    $process.Dispose()
}
