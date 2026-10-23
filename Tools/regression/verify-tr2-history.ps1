#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [Parameter(Mandatory)][string]$OutputDirectory, [switch]$IncludePostChain)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory "$out/Project/Assets" -Force | Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$out/Project" -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$out/Project/Assets/Shaders" -Recurse
$commands=@('dx12.temporalhistory','dx12.meshletcontract')
if($IncludePostChain){$commands+='dx12.post'}
$commands+='dx12.validation'
$commands | Set-Content "$out/scenario.txt" -Encoding utf8
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$saved=@{}
foreach($name in @('CREATOR_TEMPORAL_RUNTIME_DIRECTORY','CREATOR_DX12_VALIDATION','CREATOR_EDITOR_WORKSPACE_DIR','CREATOR_EDITOR_LEGACY_INI')) {
    $saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$result=[ordered]@{passed=$false;configuration=$Configuration;binarySha256=(Get-FileHash "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll").Hash}
try {
    $env:CREATOR_TEMPORAL_RUNTIME_DIRECTORY="$repo/Bin/x64-$Configuration/Editor/Temporal"
    $env:CREATOR_DX12_VALIDATION='basic'
    $env:CREATOR_EDITOR_WORKSPACE_DIR="$out/workspace"
    $env:CREATOR_EDITOR_LEGACY_INI="$out/workspace/legacy.ini"
    $arguments=@('--development-project',"$out/Project",'--smoke-offscreen','--commandlet-script',"$out/scenario.txt",'--result-format','jsonl','--result-file',"$out/commands.jsonl")
    $quoted=@($arguments|ForEach-Object {'"'+$_+'"'})
    $p=Start-Process $exe -ArgumentList $quoted -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log" -PassThru
    if(!$p.WaitForExit(300000)){throw 'Temporal history commandlet timed out.'}
    $result.exitCode=$p.ExitCode
    if($p.ExitCode -ne 0){throw "Editor exited with $($p.ExitCode)."}
    $records=@(Get-Content "$out/commands.jsonl"|ForEach-Object {$_|ConvertFrom-Json})
    foreach($command in $commands) {
        $record=@($records|Where-Object command -eq $command)
        if($record.Count -ne 1 -or $record[0].status -ne 'succeeded'){throw "$command failed or missing."}
    }
    $history=@($records|Where-Object command -eq 'dx12.temporalhistory')[0]
    if($history.data.passed -ne $true){throw 'Temporal history checks failed.'}
    $meshlet=@($records|Where-Object command -eq 'dx12.meshletcontract')[0]
    if($meshlet.data.passed -ne $true){throw 'Meshlet codec diagnostic regression failed.'}
    if($IncludePostChain) {
        $post=@($records|Where-Object command -eq 'dx12.post')[0]
        if($post.data.passed -ne $true){throw 'Post-chain pixel regression failed.'}
        $result.postChainPassed=$true
    }
    $validation=@($records|Where-Object command -eq 'dx12.validation')[0].data
    if(!$validation.layerEnabled -or $validation.messages -ne 0 -or $validation.problems -ne 0 -or $validation.droppedMessages -ne 0){throw 'DX12 validation was not clean.'}
    $result.passed=$true
} finally {
    foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    $result|ConvertTo-Json -Depth 10|Set-Content "$out/result.json"
}
