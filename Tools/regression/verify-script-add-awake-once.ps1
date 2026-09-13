[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'script-add-once'),
    [int]$TimeoutSeconds = 300
)
# The shared authoring path now rejects unregistered types before attachment.
# Count native initialization attempts on a valid component (including rejected
# duplicate lifecycle entries), and separately verify the invalid type leaves none.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the editor before this isolated gate.' }
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$settings = Join-Path $repo 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$ini = Join-Path (Split-Path $Exe) 'Saved/Config/imgui.ini'
$settingsBytes = [IO.File]::ReadAllBytes($settings)
$hadIni = Test-Path -LiteralPath $ini
$iniBytes = if ($hadIni) { [IO.File]::ReadAllBytes($ini) } else { [byte[]]@() }
$resultPath = Join-Path $Work 'script_add_awake_once.results.jsonl'
if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath }
$owned = $null
try {
    $scenario = Join-Path $repo 'scripts/script_add_awake_once.txt'
    $owned = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', ('"'+$scenario+'"'), '--result-file', ('"'+$resultPath+'"')) -WindowStyle Hidden `
        -WorkingDirectory (Split-Path $Exe) -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err') -PassThru
    if (-not $owned.WaitForExit($TimeoutSeconds * 1000)) { throw 'Script lifecycle probe timed out' }
    $results = @(Read-CommandResults $resultPath)
    $adds = @($results | Where-Object command -eq 'script.add')
    if ($adds.Count -ne 2 -or $adds[0].code -ne 'script.type_not_found' -or $adds[0].status -ne 'invalid_arguments' -or $adds[1].status -ne 'succeeded') {
        throw 'Expected an invalid-type rejection followed by a successful valid attachment'
    }
    $status = Get-SucceededCommand $results 'script.status'
    $components = @($status.components | Where-Object owner -eq 'ScriptAddRegressionTarget')
    if (@($components | Where-Object type -eq 'NoSuchScriptType_C2_2_Regression').Count) { throw 'Invalid script left an empty component' }
    $valid = @($components | Where-Object type -eq 'Bobber')
    if (-not $valid.Count -or @($valid | Where-Object instanceId -lt 0).Count) { throw 'Valid script instance is missing' }
    if (@($valid | Where-Object initializationAttempts -gt 1).Count -or -not @($valid | Where-Object initializationAttempts -eq 1).Count) {
        throw 'Expected exactly one native initialization attempt in Play'
    }
    if ($owned.ExitCode -ne 2) { throw "Expected invalid-argument session exit 2, got $($owned.ExitCode)" }
    Write-Output 'SCRIPT_ADD_ONCE_OK valid initialization exactly once; invalid type creates no component'
} finally {
    if ($null -ne $owned -and -not $owned.HasExited) { $owned.Kill(); $owned.WaitForExit() }
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
    if ($hadIni) { [IO.File]::WriteAllBytes($ini,$iniBytes) }
}