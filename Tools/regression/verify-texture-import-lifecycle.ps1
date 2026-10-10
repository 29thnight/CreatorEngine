[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [ValidateRange(1,20)][int]$Repetitions = 1,
    [switch]$SkipProjectReferences
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
$output = Join-Path $repo 'Build/TextureImportLifecycle'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sourcePaths = @('Editor/EngineEntry/EditorAssetDatabase.cpp', 'Editor/EngineEntry/EditorAssetDatabase.h',
    'Engine/RenderEngine/AssetDepot/AssetDepot.cpp', 'Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp',
    'Engine/RenderEngine/Experiment/Cooked/TextureCookProducer.cpp',
    'Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp',
    'Tools/regression/texture_import_lifecycle_probe.cpp',
    'Tools/regression/TextureImportLifecycleProbe.vcxproj', 'Tools/regression/verify-texture-import-lifecycle.ps1')
$sources = @($sourcePaths | ForEach-Object {
    $path = Join-Path $repo $_
    [pscustomobject]@{ path=$path; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
})
$options = @()
if ($SkipProjectReferences) {
    $options += '/p:BuildProjectReferences=false'
}
$receipts = @()
$previousPath = $env:PATH
try {
    foreach ($config in $configs) {
        & $msbuild (Join-Path $repo 'Tools/regression/TextureImportLifecycleProbe.vcxproj') /t:Build `
            "/p:Configuration=$config" /p:Platform=x64 /m:2 /nologo /v:minimal @options `
            *> (Join-Path $output "build-$config.log")
        if ($LASTEXITCODE -ne 0) {
            throw "Lifecycle probe build failed: $output/build-$config.log"
        }
        $vendor = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
        if (!(Test-Path -LiteralPath "$vendor/include")) {
            $vendor = Join-Path $repo 'vcpkg_installed/x64-windows'
        }
        $vendorBin = if ($config -eq 'Debug') { Join-Path $vendor 'debug/bin' } else { Join-Path $vendor 'bin' }
        $env:PATH = $vendorBin + ';' + (Join-Path $repo "Bin/x64-$config/Runtime/Common") + ';' + $previousPath
        $exe = Join-Path $repo "Bin/x64-$config/Tools/TextureImportLifecycleProbe/TextureImportLifecycleProbe.exe"
        foreach ($iteration in 1..$Repetitions) {
        foreach ($layout in @('short', 'long')) {
            # The long case reproduces the duplicated GUID-path regression.
            # Both roots are fresh owned directories inside Build; never delete them.
            $name = if ($layout -eq 'short') { 'TI-' + [Guid]::NewGuid().ToString('N').Substring(0,8) } else {
                'Phase12ImportLifecycle-' + $config + '-' + [Guid]::NewGuid().ToString('N')
            }
            $root = Join-Path $repo "Build/$name"
            New-Item -ItemType Directory -Path $root | Out-Null
            $log = Join-Path $output "run-$config-$layout-$iteration.log"
            & $exe $root (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png') *> $log
            $exit = $LASTEXITCODE
            $text = [IO.File]::ReadAllText($log)
            $match = [regex]::Match($text, 'TEXTURE_IMPORT_LIFECYCLE_OK checks=(\d+)')
            if ($exit -ne 0 -or !$match.Success -or [int]$match.Groups[1].Value -lt 36) {
                throw "Lifecycle probe failed: exit=$exit, $log"
            }
            $receipts += [pscustomobject]@{
                configuration=$config; layout=$layout; iteration=$iteration; root=$root; log=$log; checks=[int]$match.Groups[1].Value
                binary=$exe; binarySha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
            }
            "TEXTURE_IMPORT_LIFECYCLE_OK configuration=$config layout=$layout iteration=$iteration checks=$($match.Groups[1].Value)"
        }
        }
    }
} finally {
    $env:PATH = $previousPath
}
foreach ($entry in $sources) {
    if ((Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Source drift during lifecycle validation: $($entry.path)"
    }
}
[ordered]@{ receipts=$receipts; sources=$sources; sourceDrift=0; scope='Editor asset service; no ImGui or scene reload UI' } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding utf8
exit 0
