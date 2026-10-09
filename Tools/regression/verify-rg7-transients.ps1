param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [switch]$IncludeFullRegression,
    [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$out = Join-Path $repo "Build/Verification/Phase43/RG7/$Configuration"
if ($OutputDirectory)
{
    $out = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $out)
    {
        throw 'Use a fresh evidence directory.'
    }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$commandFile = Join-Path $out 'commands.txt'
$resultFile = Join-Path $out 'results.jsonl'
$commands = @('wait 60')
if ($IncludeFullRegression)
{
    $commands += 'dx12.rendergraph'
}
$commands += @('dx12.rendergraph transient', 'wait 60', 'quit')
$commands | Set-Content $commandFile -Encoding utf8
$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $out ('workspace-' + [guid]::NewGuid().ToString('N'))
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $env:CREATOR_EDITOR_WORKSPACE_DIR 'legacy.ini'
$env:CREATOR_DX12_VALIDATION = 'gpu'
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
@{ configuration=$Configuration; head=(git -C $repo rev-parse HEAD); validation='gpu';
    exe=(Get-FileHash -LiteralPath $exe).Hash;
    runtime=(Get-FileHash -LiteralPath (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;
    fullRegression=$IncludeFullRegression.IsPresent } | ConvertTo-Json | Set-Content (Join-Path $out 'binary-metadata.json') -Encoding utf8
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
    -ArgumentList @('--commandlet-script', ('"'+$commandFile+'"'), '--result-format', 'jsonl', '--result-file', ('"'+$resultFile+'"')) `
    -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
$deadline = (Get-Date).AddSeconds($(if ($IncludeFullRegression) { 600 } else { 240 }))
while (-not $process.WaitForExit(1000))
{
    if ((Get-Date) -ge $deadline)
    {
        $process.Kill()
        throw 'RG7 Editor probe timed out'
    }
}
$process.WaitForExit()
$results = @(Read-CommandResults $resultFile)
$probe = @($results | Where-Object { $_.command -eq 'dx12.rendergraph' -and $_.data.log -match 'RG7_TRANSIENT_' })
if ($process.ExitCode -ne 0 -or $probe.Count -ne 1 -or $probe[0].status -ne 'succeeded' -or
    @($results | Where-Object status -ne 'succeeded').Count -ne 0 -or
    -not $probe[0].data.passed -or $probe[0].data.log -notmatch 'RG7_TRANSIENT_OK')
{
    throw "RG7 Editor acceptance failed exit=$($process.ExitCode): $($probe | ConvertTo-Json -Depth 12)"
}
$probe[0].data.log
"RG7_EDITOR_OK configuration=$Configuration"
