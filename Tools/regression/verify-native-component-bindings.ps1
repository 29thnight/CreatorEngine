[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path $repo ('Build/Verification/NativeComponents-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $work 'Project'
$backup = Join-Path $work 'backup'
$source = Join-Path $work 'Managed'
New-Item -ItemType Directory "$project/Assets", $backup, $source -Force | Out-Null
Copy-Item -LiteralPath "$repo/Dynamic_CPP/ProjectSetting" -Destination $project -Recurse
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders" -Destination "$project/Assets" -Recurse
$fixture = Join-Path $PSScriptRoot 'fixtures/NativeComponentBindings'
& dotnet build "$repo/ScriptNative.GameScripts/ScriptNative.GameScripts.fsproj" -c $Configuration "-p:CreatorScriptSourceRoot=$fixture" "-p:CreatorNativeGameOutDir=$source/" "-p:CreatorScriptBuildKey=NativeComponents-$([guid]::NewGuid().ToString('N'))" --nologo -v:q *> "$work/build.log"
if ($LASTEXITCODE -ne 0) { throw "Fixture build failed: $work/build.log" }
$target = Join-Path $repo "Bin/x64-$Configuration/Managed/FSharpNative/Scripts"
$files = @('GameScripts.dll', 'GameScripts.deps.json', 'GameScripts.pdb', 'GameScripts.scripts.metadata.json')
foreach ($name in $files) {
    if (!(Test-Path -LiteralPath "$target/$name") -or !(Test-Path -LiteralPath "$source/$name")) { throw "Fixture file missing: $name" }
    Copy-Item -LiteralPath "$target/$name" -Destination "$backup/$name"
}
try {
    foreach ($name in $files) { Copy-Item -LiteralPath "$source/$name" -Destination "$target/$name" -Force }
    $exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
    $scenario = Join-Path $PSScriptRoot 'fixtures/native-component-bindings-smoke.txt'
    $process = Start-Process $exe -ArgumentList @('--development-project', ('"' + $project + '"'), '--commandlet-script', ('"' + $scenario + '"')) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -RedirectStandardOutput "$work/editor.out" -RedirectStandardError "$work/editor.err" -PassThru
    if (!$process.WaitForExit(180000)) { $process.Kill(); $process.WaitForExit(); throw 'Editor timed out' }
    if ($process.ExitCode -ne 0) { throw "Editor exited with $($process.ExitCode): $work" }
    $logs = @(Get-ChildItem "$project/Saved/Editor/Log" -Filter 'Editor_*.html')
    if ($logs.Count -ne 1 -or !(Select-String -LiteralPath $logs[0].FullName -SimpleMatch '[NativeComponentBindingsProbe] OK' -Quiet)) { throw "Native component result missing: $work" }
    if (Select-String -LiteralPath $logs[0].FullName -Pattern '\[NativeComponentBindingsProbe\] (missing|round trip failed)' -Quiet) { throw "Native component assertion failed: $work" }
    "NATIVE_COMPONENT_BINDINGS_OK evidence=$work"
} finally {
    foreach ($name in $files) { Copy-Item -LiteralPath "$backup/$name" -Destination "$target/$name" -Force }
    foreach ($name in $files) {
        if ((Get-FileHash "$backup/$name").Hash -ne (Get-FileHash "$target/$name").Hash) { throw "Failed to restore $name" }
    }
}
