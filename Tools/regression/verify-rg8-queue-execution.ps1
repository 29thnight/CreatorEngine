#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$FullRegression
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
    $probe = @($results | Where-Object { $_.command -eq 'dx12.rendergraph' -and $_.data.log -match 'RG8_QUEUE_EXECUTION_' })
    if ($process.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or $probe.Count -ne 1 -or
        @($results | Where-Object status -ne 'succeeded').Count -or
        !$probe[0].data.passed -or
        $probe[0].data.log -notmatch 'RG8_QUEUE_EXECUTION_OK checks=\d+ payloadError=0 validationErrors=0 modes=2 delayedLifetime=true quarantineReleased=true frameRetirement=true')
    {
        throw 'Queue execution acceptance failed or tested header is stale.'
    }
    [ordered]@{passed=$true; phaseComplete=$false; checks=[int]([regex]::Match($probe[0].data.log, 'checks=(\d+)').Groups[1].Value); scope='Native queue execution in existing offscreen Editor';
        fullRegression=[bool]$FullRegression; nativeQueueExecutionTested=$true; frameRetirementTested=$true;
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
