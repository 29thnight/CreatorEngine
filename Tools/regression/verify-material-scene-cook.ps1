param(
    [string]$VisualStudioInstallation = '',
    [switch]$SkipDependencyRestore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$msbuild = Join-Path $VisualStudioInstallation 'MSBuild\Current\Bin\MSBuild.exe'
$output = Join-Path $repo 'Build\Obj\MaterialProductProbe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    Get-ChildItem (Join-Path $repo 'Engine\RenderEngine'), (Join-Path $repo 'Engine\Utility_Framework'),
        (Join-Path $repo 'Lattice'), (Join-Path $repo 'Tools\AssetCooker'),
        (Join-Path $repo 'Dynamic_CPP\Assets\Shaders\DefaultPassShader') -Recurse -File |
        Where-Object Extension -In '.cpp', '.h', '.slang', '.vcxproj', '.props', '.targets'
    Get-ChildItem $PSScriptRoot -File | Where-Object Name -Match '^material_(raster|scene|ibl|runtime)|^principled_layered_reference'
    Get-Item (Join-Path $PSScriptRoot 'MaterialRasterSurfaceProbe.vcxproj'), $PSCommandPath,
        (Join-Path $repo 'Directory.Build.props'), (Join-Path $repo 'Directory.Build.targets'),
        (Join-Path $repo 'EngineOutput.props')
) | Sort-Object FullName -Unique
$snapshot = @($sources | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'scene-cook-source-hashes.json') -Encoding utf8
$dependencyOptions = @()
if ($SkipDependencyRestore) { $dependencyOptions += '/p:VcpkgManifestInstall=false' }

foreach ($configuration in @('Debug', 'Release')) {
    foreach ($project in @('Tools/regression/MaterialRasterSurfaceProbe.vcxproj', 'Tools/AssetCooker/AssetCooker.vcxproj')) {
        $build = @(& $msbuild (Join-Path $repo $project) /m:2 /nologo "/p:Configuration=$configuration" `
            /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false @dependencyOptions /v:minimal 2>&1)
        $buildExit = $LASTEXITCODE
        $build | Set-Content -LiteralPath (Join-Path $output "scene-cook-build-$([IO.Path]::GetFileNameWithoutExtension($project))-$configuration.log") -Encoding utf8
        if ($buildExit -ne 0) { throw "Scene cook build failed: $project/$configuration" }
    }
    $probe = Join-Path $repo "Bin/x64-$configuration/Tools/MaterialRasterSurfaceProbe/MaterialRasterSurfaceProbe.exe"
    $cooker = Join-Path $repo "Bin/x64-$configuration/Tools/AssetCooker/AssetCooker.exe"
    $caseRoot = Join-Path $output ("SceneCook-$configuration-" + [Guid]::NewGuid().ToString('N'))
    $assets = Join-Path $caseRoot 'Assets'
    New-Item -ItemType Directory -Path $assets | Out-Null
    $previousPath = $env:PATH
    $previousValidation = $env:CREATOR_DX12_VALIDATION
    try {
        $dependencyRoot = if ($configuration -eq 'Debug') { 'vcpkg_installed/x64-windows/debug/bin' } else { 'vcpkg_installed/x64-windows/bin' }
        $env:PATH = (Join-Path $repo $dependencyRoot) + ';' + $previousPath
        $env:CREATOR_DX12_VALIDATION = 'gpu'
        $export = @(& $probe $repo --export-cook-fixtures 2>&1)
        if ($LASTEXITCODE -ne 0 -or !($export -match '^LX_SCENE_COOK_FIXTURES_OK graphs=2$')) { throw 'Fixture export failed.' }
        foreach ($tier in @('core', 'layered')) {
            foreach ($extension in @('.shadergraph', '.shadergraph.meta')) {
                Copy-Item -LiteralPath (Join-Path $output "scene-cook-$tier$extension") -Destination (Join-Path $assets "$tier$extension")
            }
        }
        [IO.File]::WriteAllBytes((Join-Path $assets 'fixture.png'), [Convert]::FromBase64String(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jVI4AAAAASUVORK5CYII='))
        'guid: 22222222-2222-4222-8222-222222222222' | Set-Content (Join-Path $assets 'fixture.png.meta') -Encoding utf8
        $materialText = @'
lattice_material: 1
name: CookFixture
assetId: 44444444-4444-4444-8444-444444444444
graphAssetId: 11111111-1111-4111-8111-111111111111
doubleSided: true
parameters:
  - id: 900
    float: 1.7
textures: []
'@
        $material = Join-Path $assets 'fixture.asset'
        [IO.File]::WriteAllText($material, $materialText)
        'guid: 44444444-4444-4444-8444-444444444444' | Set-Content "$material.meta" -Encoding utf8
        # Also exercise typed graph dependency extraction inside an inline Scene material.
        $scene = Join-Path $assets 'fixture.creator'
        $inlineText = $materialText.Replace('44444444-4444-4444-8444-444444444444', '00000000-0000-0000-0000-000000000000')
        [IO.File]::WriteAllText($scene, "m_Entities:`n  - m_components:`n      - MeshRenderer: 1`n        m_Material:`n" +
            (($inlineText -split '\r?\n' | ForEach-Object { '          ' + $_ }) -join "`n"))
        'guid: 66666666-6666-4666-8666-666666666666' | Set-Content "$scene.meta" -Encoding utf8
        $common = @('--asset-root', $assets, '--material-shader-root', (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader'),
            '--shadergraph', 'core.shadergraph', '--shadergraph', 'layered.shadergraph', '--texture', (Join-Path $assets 'fixture.png'),
            '--material', $material, '--scene', $scene)
        $accepted = Join-Path $caseRoot 'Accepted'
        foreach ($destination in @($accepted, (Join-Path $caseRoot 'Repeated'))) {
            $result = @(& $cooker @common --output $destination 2>&1)
            $cookExit = $LASTEXITCODE
            $result | Set-Content (Join-Path $caseRoot "$([IO.Path]::GetFileName($destination)).log") -Encoding utf8
            if ($cookExit -ne 0 -or !($result -match 'materialPrograms=2') -or !($result -match 'artifactPaths=5')) {
                throw "Automatic Scene cook failed: $configuration/$destination $($result -join "`n")"
            }
        }
        $hashes = @(Get-ChildItem $accepted -Recurse -File | ForEach-Object {
            $relative = [IO.Path]::GetRelativePath($accepted, $_.FullName)
            $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
            if ((Get-FileHash (Join-Path $caseRoot "Repeated/$relative") -Algorithm SHA256).Hash -ne $hash) {
                throw "Repeated cook differs: $relative"
            }
            [pscustomobject]@{ path = $_.FullName; hash = $hash }
        })
        foreach ($failure in @('unknown-parameter', 'missing-graph', 'missing-texture', 'wrong-identity', 'missing-host', 'unknown-node')) {
            $arguments = @($common)
            $graphText = [IO.File]::ReadAllText((Join-Path $assets 'core.shadergraph'))
            try {
                switch ($failure) {
                    'unknown-parameter' { [IO.File]::WriteAllText($material, $materialText.Replace('id: 900', 'id: 999999')) }
                    'missing-graph' { [IO.File]::WriteAllText($material, $materialText.Replace('11111111-1111-4111-8111-111111111111', '77777777-7777-4777-8777-777777777777')) }
                    'missing-texture' {
                        $index = [Array]::IndexOf($arguments, '--texture')
                        $arguments = @($arguments[0..($index - 1)] + $arguments[($index + 2)..($arguments.Length - 1)])
                    }
                    'wrong-identity' { [IO.File]::WriteAllText($material, $materialText.Replace('44444444-4444-4444-8444-444444444444', '55555555-5555-4555-8555-555555555555')) }
                    'missing-host' { $arguments[3] = Join-Path $caseRoot 'MissingShaderHost' }
                    'unknown-node' {
                        $changed = $graphText | ConvertFrom-Json
                        $changed.graph.nodes[0].definition = 'invalid.cook.node'
                        [IO.File]::WriteAllText((Join-Path $assets 'core.shadergraph'), ($changed | ConvertTo-Json -Depth 100))
                    }
                }
                $rejected = Join-Path $caseRoot $failure
                $result = @(& $cooker @arguments --output $rejected 2>&1)
                $rejectedExit = $LASTEXITCODE
            } finally {
                [IO.File]::WriteAllText($material, $materialText)
                [IO.File]::WriteAllText((Join-Path $assets 'core.shadergraph'), $graphText)
            }
            $result | Set-Content (Join-Path $caseRoot "$failure.log") -Encoding utf8
            if ($rejectedExit -eq 0 -or (Test-Path -LiteralPath $rejected)) { throw "Rejected cook published: $configuration/$failure" }
            foreach ($entry in $hashes) {
                if ((Get-FileHash $entry.path -Algorithm SHA256).Hash -ne $entry.hash) { throw "Failure changed accepted output: $failure" }
            }
        }
        $runtime = @(& $probe $repo --cooked-scene $accepted 2>&1)
        $runtimeExit = $LASTEXITCODE
        $runtime | Set-Content (Join-Path $output "scene-cook-runtime-$configuration.log") -Encoding utf8
        if ($runtimeExit -ne 0 -or !($runtime -match '^LX_MATERIAL_COOKED_SCENE_OK frames=24 .*sceneCompiles=0 validation=0 pak=encrypted$') -or
            !($runtime -match 'DebugLayer=on GPUValidation=on')) { throw "Cooked native Scene failed: $configuration $($runtime -join "`n")" }
        $runtime | Where-Object { $_ -match '^LX_MATERIAL_.*_OK ' }
        $caseRoot | Set-Content (Join-Path $output "scene-cook-$configuration-root.txt") -Encoding utf8
        "LX_MATERIAL_SCENE_COOK_CONFIGURATION_OK configuration=$configuration artifacts=5 repeatedBytes=identical rejected=6 preserved=true"
    } finally {
        $env:PATH = $previousPath
        $env:CREATOR_DX12_VALIDATION = $previousValidation
    }
}
foreach ($entry in $snapshot) {
    if ((Get-FileHash $entry.path -Algorithm SHA256).Hash -ne $entry.hash) { throw "Scene cook source changed: $($entry.path)" }
}
"LX_MATERIAL_SCENE_COOK_GATE_OK sources=$($snapshot.Count) drift=0 configurations=Debug,Release"
exit 0
