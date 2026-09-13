[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution,[Parameter(Mandatory)][string]$Project)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $repo 'Tools/distribution/EngineDistribution.psm1')
$manifest=Read-EngineDistribution $EngineDistribution
Assert-EngineProjectPin $Project $manifest -Require
$managed=Join-Path $Project "Intermediate/Managed/$($manifest.configuration)"
$buildTool=Join-Path $EngineDistribution $manifest.buildTool
$oldPath=$env:PATH
try {
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
    & $buildTool compile-game -Project $Project -EngineDistribution $EngineDistribution -Output $managed -Config $manifest.configuration
    if ($LASTEXITCODE -ne 0) { throw 'External Editor scripts did not compile.' }
} finally { $env:PATH=$oldPath }
$scriptPath=Join-Path $Project "Intermediate/editor-distribution-$($manifest.configuration)-test.txt"
$resultPath=Join-Path $Project "Intermediate/editor-distribution-$($manifest.configuration)-test.jsonl"
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath -Force }
[IO.File]::WriteAllLines($scriptPath,@('wait 60','editor.theme','quit'),[Text.UTF8Encoding]::new($false))
$start=[Diagnostics.ProcessStartInfo]::new((Join-Path $EngineDistribution "$($manifest.binaryRoot)/Editor/CreatorEditor.exe"))
foreach ($argument in @('--development-project',$Project,'--managed-root',$managed,'--script',$scriptPath,'--result-format','jsonl','--result-file',$resultPath)) { $start.ArgumentList.Add($argument) }
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.WindowStyle='Hidden'
$start.WorkingDirectory=$Project
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.StandardOutputEncoding=[Text.Encoding]::UTF8
$start.StandardErrorEncoding=[Text.Encoding]::UTF8
$start.Environment['PATH']="$env:SystemRoot\System32;$env:SystemRoot"
$start.Environment['DOTNET_ROOT']=Join-Path $Project 'NoSystemDotnet'
$start.Environment['DOTNET_MULTILEVEL_LOOKUP']='0'
$process=[Diagnostics.Process]::Start($start)
$stdout=$process.StandardOutput.ReadToEndAsync()
$stderr=$process.StandardError.ReadToEndAsync()
$elapsed=[Diagnostics.Stopwatch]::StartNew()
while (-not $process.WaitForExit(30000)) {
    if ($elapsed.Elapsed.TotalSeconds -gt 300) { $process.Kill($true); $process.WaitForExit(); break }
    Write-Host "External Editor validation running: $Project"
}
[IO.File]::WriteAllText((Join-Path $Project "Intermediate/editor-distribution-$($manifest.configuration).stdout.log"),$stdout.GetAwaiter().GetResult())
[IO.File]::WriteAllText((Join-Path $Project "Intermediate/editor-distribution-$($manifest.configuration).stderr.log"),$stderr.GetAwaiter().GetResult())
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $resultPath)) { throw "External Editor failed ($($process.ExitCode)): $Project" }
$rows=@(Get-Content -LiteralPath $resultPath | ForEach-Object { $_ | ConvertFrom-Json })
if (-not @($rows | Where-Object { $_.command -eq 'editor.theme' -and $_.status -eq 'succeeded' }).Count) { throw 'Editor did not produce a successful theme result.' }
# Opening a project must not write cache/log/layout data into the installed engine.
[void](Read-EngineDistribution $EngineDistribution)
Write-Host "PREBUILT_EDITOR_VERIFIED $($manifest.configuration) $Project"
