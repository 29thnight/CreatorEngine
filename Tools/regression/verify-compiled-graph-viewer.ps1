#Requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo 'Artifacts/compiled-graph-viewer-probe'
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $out -Force | Out-Null
@{ passed = $false; state = 'running' } | ConvertTo-Json | Set-Content "$out/result.json" -Encoding utf8
$vs = 'C:/Program Files/Microsoft Visual Studio/18/Community'
& "$vs/Common7/Tools/Launch-VsDevShell.ps1" -VsInstallationPath $vs -Arch x64 -HostArch x64 -NoLogo -SkipAutomaticLocation
$packages = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
Push-Location $repo
try {
    & cl /nologo /utf-8 /std:c++20 /EHsc /W4 Tools/regression/compiled_graph_viewer_probe.cpp "/Fe:$out/model-probe.exe" "/Fo:$out/model-probe.obj" 2>&1 | Tee-Object "$out/model-build.log"
    if ($LASTEXITCODE -ne 0) { throw 'Compiled graph model probe did not build.' }
    & "$out/model-probe.exe" | Tee-Object "$out/model-result.log"
    if ($LASTEXITCODE -ne 0) { throw 'Compiled graph model probe failed.' }
    & cl /nologo /utf-8 /std:c++20 /EHsc /MDd /DMATHEMATICS_NO_PERF_WARNINGS "/I$repo/Engine/RuntimeHost" "/I$repo/Engine/Utility_Framework" "/I$repo/Engine/RenderEngine" "/I$repo/Engine/RenderEngine/Interfaces" "/I$repo/ThirdParty/Mathematics/include" "/I$repo/ThirdParty/GCCE/include" "/I$packages/include" Tools/regression/compiled_graph_viewer_imgui_probe.cpp "/Fe:$out/imgui-probe.exe" "/Fo:$out/imgui-probe.obj" /link "/LIBPATH:$packages/debug/lib" imguid.lib freetyped.lib 2>&1 | Tee-Object "$out/imgui-build.log"
    if ($LASTEXITCODE -ne 0) { throw 'Compiled graph ImGui probe did not build.' }
    $savedGraphViewerPath = $env:PATH
    try {
        $env:PATH = "$packages/debug/bin;" + $env:PATH
        & "$out/imgui-probe.exe" 2>&1 | Tee-Object "$out/imgui-result.log"
        if ($LASTEXITCODE -ne 0) { throw 'Compiled graph ImGui probe failed.' }
    } finally {
        $env:PATH = $savedGraphViewerPath
    }
    @{ passed = $true; model = $true; imgui = $true; actualSnapshotType = $true; passes = 80; resources = 120; sizes = 2; gpuExecution = $false } | ConvertTo-Json | Set-Content "$out/result.json" -Encoding utf8
} finally {
    Pop-Location
}
