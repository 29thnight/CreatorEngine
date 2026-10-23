#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path "$out/Project/Assets" -Force | Out-Null
Copy-Item -LiteralPath "$repo/Dynamic_CPP/ProjectSetting" -Destination "$out/Project" -Recurse
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" -Destination "$out/Project/Assets/Shaders" -Recurse
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$savedWorkspace=$env:CREATOR_EDITOR_WORKSPACE_DIR
$savedIni=$env:CREATOR_EDITOR_LEGACY_INI
try {
    $env:CREATOR_EDITOR_WORKSPACE_DIR="$out/workspace"
    $env:CREATOR_EDITOR_LEGACY_INI="$out/workspace/legacy.ini"
    & "$repo/Tools/temporal-validation/Verify-SSGIAbortRecovery.ps1" -EditorExecutable $exe -ProjectPath "$out/Project" `
        -RuntimeDirectory "$repo/Bin/x64-$Configuration/Runtime" -OutputDirectory "$out/native" -TimeoutSeconds 300
} finally {
    $env:CREATOR_EDITOR_WORKSPACE_DIR=$savedWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI=$savedIni
}
