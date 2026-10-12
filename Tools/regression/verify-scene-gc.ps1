[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(1, 1000)][int]$Iterations = 20,
    [ValidateRange(10, 1800)][int]$TimeoutSeconds = 180,
    [string]$Work = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $repository "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Editor executable is missing: $exe"
}
$runtime = Get-Item -LiteralPath (Join-Path (Split-Path $exe -Parent) 'CreatorEditor.runtime.dll')
foreach ($source in @(
    'Editor/RenderTests/Tasks/SceneGCSelfTest.cpp',
    'Editor/EngineEntry/Commands/RenderTestCommands.cpp',
    'Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp',
    'Engine/SceneRuntime/Scene.cpp',
    'Engine/SceneRuntime/Entity.cpp',
    'Engine/SceneRuntime/Component.cpp',
    'Engine/SceneRuntime/SceneManager.cpp')) {
    if ((Get-Item -LiteralPath (Join-Path $repository $source)).LastWriteTimeUtc -gt $runtime.LastWriteTimeUtc) {
        throw "Fresh $Configuration Editor build required: $source is newer than the runtime."
    }
}
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw 'Close the existing editor before running this isolated gate.'
}
if (-not $Work) {
    $Work = Join-Path $repository "Artifacts/scene-gc/$Configuration"
}
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$scriptPath = Join-Path $Work 'scene-gc.commands.txt'
$outPath = Join-Path $Work 'scene-gc.out.log'
$errPath = Join-Path $Work 'scene-gc.err.log'
@('scene.gc.selftest') * $Iterations | Set-Content -LiteralPath $scriptPath -Encoding utf8
$priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$priorLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $Work 'workspace'
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work 'workspace/legacy.ini'
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
$process = $null
try {
    $process = Start-Process -FilePath $exe -ArgumentList @('--commandlet-script', ('"' + $scriptPath + '"')) `
        -WorkingDirectory (Split-Path $exe -Parent) -WindowStyle Hidden `
        -RedirectStandardOutput $outPath -RedirectStandardError $errPath -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.WaitForExit(1000)) {
        if ((Get-Date) -ge $deadline) {
            throw "Scene GC gate timed out after $TimeoutSeconds seconds. See $outPath and $errPath"
        }
    }
    $process.Refresh()
    $output = Get-Content -LiteralPath $outPath -Raw
    $successes = [regex]::Matches($output, '(?m)^SCENE_GC_OK cycle=2 lifecycle=2 staleHandles=2 selection=2 ddol=1 workerPin=1 incrementalSteps=[1-9][0-9]*\r?$').Count
    if ($process.ExitCode -ne 0 -or $successes -ne $Iterations -or $output.Contains('SCENE_GC_FAILED')) {
        throw "Scene GC gate failed: exit=$($process.ExitCode), passed=$successes/$Iterations. See $outPath and $errPath"
    }
    "PASS Scene GC $Configuration iterations=$successes exit=$($process.ExitCode)"
} finally {
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacyIni
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit(15000) | Out-Null
        }
        $process.Dispose()
    }
}
