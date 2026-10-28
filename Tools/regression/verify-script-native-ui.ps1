[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CorpusManagedDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('UI','Animator')][string]$Probe = 'UI'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = [IO.Path]::GetFullPath($CorpusManagedDirectory)
$work = Join-Path $repo ('Build/Verification/NativeUi-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $work 'Project'
$backup = Join-Path $work 'backup'
New-Item -ItemType Directory "$project/Assets", $backup -Force | Out-Null
Copy-Item -LiteralPath "$repo/Dynamic_CPP/ProjectSetting" -Destination $project -Recurse
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" -Destination "$project/Assets" -Recurse
$target = Join-Path $repo "Bin/x64-$Configuration/Managed/FSharpNative/Scripts"
$files = @('GameScripts.dll', 'GameScripts.deps.json', 'GameScripts.pdb', 'GameScripts.scripts.metadata.json')
foreach ($name in $files) {
    if (!(Test-Path -LiteralPath "$target/$name") -or !(Test-Path -LiteralPath "$source/$name")) { throw "UI fixture file missing: $name" }
    Copy-Item -LiteralPath "$target/$name" -Destination "$backup/$name"
}
try {
    foreach ($name in $files) { Copy-Item -LiteralPath "$source/$name" -Destination "$target/$name" -Force }
    $exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
    $scenario = Join-Path $PSScriptRoot $(if ($Probe -eq 'UI') { 'fixtures/fsharp-ui-smoke.txt' } else { 'fixtures/fsharp-animator-smoke.txt' })
    $process = Start-Process $exe -ArgumentList @('--development-project', ('"' + $project + '"'), '--commandlet-script', ('"' + $scenario + '"')) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$work/editor.out" -RedirectStandardError "$work/editor.err" -PassThru
    if (!$process.WaitForExit(180000)) { $process.Kill(); $process.WaitForExit(); throw 'UI editor timed out' }
    if ($process.ExitCode -ne 0) { throw "UI editor exited with $($process.ExitCode)" }
    $logs = @(Get-ChildItem "$project/Saved/Editor/Log" -Filter 'Editor_*.html')
    $success = if ($Probe -eq 'UI') { '\[UiProbe\] 전체 통과 \(7건\)' } else { '\[AnimatorProbe\] 전체 통과 \(3건\)' }
    $failure = if ($Probe -eq 'UI') { '\[UiProbe\].*실패|\[UiProbe\] (RectTransformComponent|ImageComponent) 없음' } else { '\[AnimatorProbe\].*실패|\[AnimatorProbe\] Animator가 없습니다' }
    if ($logs.Count -ne 1 -or !(Select-String -LiteralPath $logs[0].FullName -Pattern $success -Quiet)) { throw "$Probe result missing: $work" }
    if (Select-String -LiteralPath $logs[0].FullName -Pattern $failure -Quiet) { throw "$Probe assertion or component missing: $work" }
    "NATIVE_${Probe}_OK evidence=$work"
} finally {
    foreach ($name in $files) { Copy-Item -LiteralPath "$backup/$name" -Destination "$target/$name" -Force }
    foreach ($name in $files) {
        if ((Get-FileHash "$backup/$name").Hash -ne (Get-FileHash "$target/$name").Hash) { throw "Failed to restore $name" }
    }
}
