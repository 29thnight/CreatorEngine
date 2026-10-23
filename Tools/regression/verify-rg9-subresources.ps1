#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory $out | Out-Null
@('dx12.rendergraph subresources','quit') | Set-Content "$out/commands.txt" -Encoding utf8
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$start=[Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory=Split-Path $exe
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
foreach($argument in @('--smoke-offscreen','--commandlet-script',"$out/commands.txt",'--result-format','jsonl','--result-file',"$out/results.jsonl"))
{
    $start.ArgumentList.Add($argument)
}
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR']="$out/workspace"
$start.Environment['CREATOR_EDITOR_LEGACY_INI']="$out/workspace/legacy.ini"
$start.Environment['CREATOR_DX12_VALIDATION']='gpu'
$start.Environment.Remove('CREATOR_GPU_MEMORY_SAMPLES') | Out-Null
$start.Environment.Remove('CREATOR_RENDERGRAPH_ALIASING') | Out-Null
@{head=(git -C $repo rev-parse HEAD);configuration=$Configuration;exe=(Get-FileHash $exe).Hash;runtime=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;test=(Get-FileHash (Join-Path $repo 'Editor/RenderTests/RHI/DX12/Tests/RenderSubresourceRg9Tests.h')).Hash} | ConvertTo-Json | Set-Content "$out/binary.json"
$process=[Diagnostics.Process]::Start($start)
$stdout=$process.StandardOutput.ReadToEndAsync()
$stderr=$process.StandardError.ReadToEndAsync()
try
{
    $deadline=[DateTime]::UtcNow.AddMinutes(10)
    while(!$process.WaitForExit(1000))
    {
        if([DateTime]::UtcNow -ge $deadline){throw 'RG9 native probe timed out.'}
    }
    $output=$stdout.GetAwaiter().GetResult()
    $output | Set-Content "$out/stdout.log"
    $stderr.GetAwaiter().GetResult() | Set-Content "$out/stderr.log"
    $results=@(Read-CommandResults "$out/results.jsonl")
    if($process.ExitCode -ne 0 -or $results.Count -ne 2){throw 'RG9 terminal result/exit mismatch.'}
    foreach($result in $results)
    {
        if($result.status -cne 'succeeded' -or $result.code -cne 'ok'){throw 'RG9 command failed.'}
    }
    if($output -notmatch 'RG9_SUBRESOURCES_OK checks=13315 frames=512 splitFrames=256 fullFrames=256 repetitions=3 mip=1 layer=1 direct=true software=true transientMips=true pixelError=0 validationErrors=0'){throw 'RG9 native acceptance marker missing.'}
    @{passed=$true;configuration=$Configuration;checks=13315;frames=512;splitFrames=256;fullFrames=256;repetitions=3;pixelError=0;validationErrors=0;exitCode=$process.ExitCode} | ConvertTo-Json | Set-Content "$out/result.json"
}
finally
{
    if(!$process.HasExited){$process.Kill($true);$process.WaitForExit()}
}
