[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CorpusManagedDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('UI','Animator','UiNav','UiText','RectLayout','EntryGuard','Curve')][string]$Probe = 'UI'
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
    $fixtures=@{
        UI=@('fsharp-ui-smoke.txt','\[UiProbe\] 전체 통과 \(7건\)','\[UiProbe\].*실패|\[UiProbe\] (RectTransformComponent|ImageComponent) 없음')
        Animator=@('fsharp-animator-smoke.txt','\[AnimatorProbe\] 전체 통과 \(3건\)','\[AnimatorProbe\].*실패|\[AnimatorProbe\] Animator가 없습니다')
        UiNav=@('fsharp-ui-nav-smoke.txt','\[UiNavProbe\] 전체 통과 \(8건\)','\[UiNavProbe\].*(실패|없음)')
        UiText=@('fsharp-ui-text-smoke.txt','\[UiTextProbe\] 전체 통과 \(10건\)','\[UiTextProbe\].*실패')
        RectLayout=@('fsharp-rect-layout-smoke.txt','\[RectLayoutProbe\] 전체 통과 \(2건\)','\[RectLayoutProbe\].*(실패|없음)')
        EntryGuard=@('fsharp-entry-guard-smoke.txt','\[LC6\] sample=15 calls=1000000','\[LC6\].*실패')
        Curve=@('fsharp-curve-smoke.txt','\[CurveIndicatorProbe\] 전체 통과 \(4건\)','\[CurveIndicatorProbe\].*(실패|찾지 못)')
    }
    $fixture=$fixtures[$Probe]
    $scenario=Join-Path $PSScriptRoot ('fixtures/'+$fixture[0])
    if($Probe -eq 'Curve'){
        New-Item -ItemType Directory -Force "$project/Assets/Scenes","$project/Assets/Materials"|Out-Null
        Copy-Item -LiteralPath "$PSScriptRoot/fixtures/material-codegen/constant.shadergraph" -Destination "$project/Assets/Materials/Curve.shadergraph"
        "guid: f3289c86-556f-8c98-9c50-c1bb354ce1fc`nimportSettings:`n  extension: .shadergraph`n  timestamp: $([DateTime]::UtcNow.ToFileTimeUtc())"|Set-Content "$project/Assets/Materials/Curve.shadergraph.meta" -Encoding utf8
        $scenePath=Join-Path $project 'Assets/Scenes/FSharpCurve.creator'
        Copy-Item -LiteralPath "$PSScriptRoot/fixtures/fsharp-curve-smoke.creator.in" -Destination $scenePath
        "guid: $([guid]::NewGuid())"|Set-Content ($scenePath+'.meta') -Encoding utf8
        $scenarioText=(Get-Content $scenario -Raw).Replace('__CURVE_SCENE__',$scenePath)
        $scenario=Join-Path $work 'curve-commands.txt'
        Set-Content $scenario $scenarioText -Encoding utf8
    }
    $process = Start-Process $exe -ArgumentList @('--development-project', ('"' + $project + '"'), '--commandlet-script', ('"' + $scenario + '"')) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$work/editor.out" -RedirectStandardError "$work/editor.err" -PassThru
    if (!$process.WaitForExit(180000)) { $process.Kill(); $process.WaitForExit(); throw 'UI editor timed out' }
    if ($process.ExitCode -ne 0) { throw "UI editor exited with $($process.ExitCode)" }
    $logs = @(Get-ChildItem "$project/Saved/Editor/Log" -Filter 'Editor_*.html')
    $success=$fixture[1]
    $failure=$fixture[2]
    if ($logs.Count -ne 1 -or !(Select-String -LiteralPath $logs[0].FullName -Pattern $success -Quiet)) { throw "$Probe result missing: $work" }
    if (Select-String -LiteralPath $logs[0].FullName -Pattern $failure -Quiet) { throw "$Probe assertion or component missing: $work" }
    "NATIVE_${Probe}_OK evidence=$work"
} finally {
    foreach ($name in $files) { Copy-Item -LiteralPath "$backup/$name" -Destination "$target/$name" -Force }
    foreach ($name in $files) {
        if ((Get-FileHash "$backup/$name").Hash -ne (Get-FileHash "$target/$name").Hash) { throw "Failed to restore $name" }
    }
}
