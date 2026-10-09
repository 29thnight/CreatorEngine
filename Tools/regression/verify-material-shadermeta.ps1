param([string]$VisualStudioInstallation = '', [switch]$VerifyAssetCooker, [switch]$VerifyConsumer, [string]$CookFixtureRoot = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GCCEProbe.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $repo 'Build\Obj\MaterialShaderMetaProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'material_shadermeta_probe.exe'
$dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
if (!(Test-Path -LiteralPath (Join-Path $dependencies 'include/reflgen/runtime/registry.h'))) {
    $dependencies = Join-Path $repo 'vcpkg_installed/x64-windows'
}
if ($CookFixtureRoot) {
    if (!$VerifyAssetCooker) { throw 'CookFixtureRoot requires VerifyAssetCooker.' }
    $work = [IO.Path]::GetFullPath($CookFixtureRoot)
    if (!(Test-Path -LiteralPath (Join-Path $work 'Assets/fixture.shadergraph'))) {
        throw 'The prior adapter run has no exported source Graph fixture.'
    }
} else {
$sources = @('Tools/regression/material_shadermeta_probe.cpp',
    'Engine/RenderEngine/MaterialGraphRuntime.cpp',
    'Engine/RenderEngine/LXMaterialRuntime.cpp',
    'Engine/Utility_Framework/TypeTrait.cpp',
    'Engine/RenderEngine/Experiment/Cooked/CookedMaterialProgram.cpp',
    'Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.cpp',
    'Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.cpp',
    'Engine/RenderEngine/Assets/AssetIdentityProfile.cpp',
    'Engine/RenderEngine/MaterialGraphShaderMeta.cpp', 'Engine/RenderEngine/ShaderMeta.cpp',
    'Engine/RenderEngine/ShaderMetaReflection.cpp', 'Engine/RenderEngine/MaterialPropertyPacker.cpp',
    'Engine/RenderEngine/MaterialGraphSceneCompiler.cpp', 'Engine/RenderEngine/MaterialGraphProduct.cpp',
    'Engine/RenderEngine/RHI/RHIShaderCompiler.cpp', 'Engine/RenderEngine/RHI/RHIShaderSource.cpp',
    'Engine/RenderEngine/RHI/RHIShaderReflection.cpp', 'Engine/RenderEngine/RHI/RHIShaderPermutation.cpp',
    'Engine/Utility_Framework/AuthoringScalarConvert.cpp', 'Engine/Utility_Framework/AuthoringRymlErrorPolicy.cpp',
    'Engine/Utility_Framework/AuthoringCookedDocument.cpp',
    'Engine/Utility_Framework/AuthoringParsedDocument.cpp', 'Engine/Utility_Framework/TimeSystem.cpp',
    'Lattice/Core/LXGraph.cpp', 'Lattice/Core/LXNodeDefinition.cpp', 'Lattice/Material/LXMaterialGraph.cpp',
    'Lattice/Material/LXMaterialIR.cpp', 'Lattice/Material/LXMaterialNodes.cpp',
    'Lattice/Material/LXMaterialOperators.cpp', 'Lattice/Material/LXMaterialCompiler.cpp')
$gcce = Get-GCCEProbeSettings -Repository $repo -Configuration Release
$quoted = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$compile = 'call "' + $vcvars + '" >nul && cl.exe ' + $gcce.CompileArguments + ' /nologo /MP2 /EHsc /std:c++latest /permissive- /utf-8 ' +
    '/DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /MD /I"' + (Join-Path $repo 'ThirdParty/Slang/include') +
    '" /I"' + (Join-Path $repo 'ThirdParty/Mathematics/include') + '" /I"' + (Join-Path $repo 'Engine/Utility_Framework') +
    '" /I"' + (Join-Path $dependencies 'include') + '" /Fo:"' + $output + '/" /Fe:"' + $exe +
    '" ' + $quoted + ' /link /LIBPATH:"' + (Join-Path $dependencies 'lib') + '" ryml.lib c4core.lib d3d12.lib dxgi.lib ole32.lib' + ' ' + $gcce.LinkArguments
$build = @(& $env:ComSpec /d /s /c $compile 2>&1)
$build | Set-Content -LiteralPath (Join-Path $output 'build.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "ShaderMeta adapter build failed. See $output\build.log" }
$work = Join-Path $output ('Run-' + [Guid]::NewGuid().ToString('N'))
$previousPath = $env:PATH
try {
    $env:PATH = $gcce.RuntimeDirectory + ';' + (Join-Path $dependencies 'bin') + ';' + $previousPath
    $result = @(& $exe $repo $work 2>&1)
    $exitCode = $LASTEXITCODE
} finally { $env:PATH = $previousPath }
$result | Set-Content -LiteralPath (Join-Path $output 'adapter.log') -Encoding utf8
if ($exitCode -ne 0 -or @($result | Where-Object { $_ -match '^MAT7_SHADERMETA_ADAPTER_OK ' }).Count -ne 1) {
    throw "ShaderMeta adapter gate failed: $($result -join "`n")"
}
$result
"MAT7_SHADERMETA_ADAPTER_ARTIFACTS path=$work"
}
if ($VerifyConsumer -and !$VerifyAssetCooker) { throw 'VerifyConsumer requires VerifyAssetCooker.' }
if ($VerifyAssetCooker) {
    foreach ($configuration in @('Debug', 'Release')) {
        $cooker = Join-Path $repo "Bin/x64-$configuration/Tools/AssetCooker/AssetCooker.exe"
        if (!(Test-Path -LiteralPath $cooker)) { throw "Build AssetCooker $configuration before the cook gate." }
        $cookRoot = Join-Path $work ("Cook-$configuration-" + [Guid]::NewGuid().ToString('N'))
        $assets = Join-Path $cookRoot 'Assets'
        New-Item -ItemType Directory -Path $assets -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $work 'Assets/fixture.shadergraph') -Destination $assets
        Copy-Item -LiteralPath (Join-Path $work 'Assets/fixture.shadergraph.meta') -Destination $assets
        $metaHash = $null
        $sourceHash = $null
        foreach ($destination in @('Accepted', 'Repeated')) {
            $previousPath = $env:PATH
            try {
                $dependencyBin = if ($configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }
                $env:PATH = (Join-Path $dependencies $dependencyBin) + ';' + $previousPath
                $cook = @(& $cooker --asset-root $assets --output (Join-Path $cookRoot $destination) `
                    --shadergraph fixture.shadergraph --material-shader-root `
                    (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader') 2>&1)
                $cookExit = $LASTEXITCODE
            } finally { $env:PATH = $previousPath }
            $cook | Set-Content -LiteralPath (Join-Path $cookRoot "$destination.log") -Encoding utf8
            if ($cookExit -ne 0 -or !($cook -match 'materialPrograms=1')) { throw "Actual $configuration cook failed: $($cook -join "`n")" }
            $generatedRoot = Join-Path $cookRoot 'Library/LXSceneCook/11111111-1111-4111-8111-111111111111.generated'
            $metas = @(Get-ChildItem -LiteralPath $generatedRoot -File -Filter material.shadermeta -Recurse)
            if ($metas.Count -ne 1) { throw "Expected one generated source pair: $configuration/$destination" }
            $meta = Get-Content -LiteralPath $metas[0].FullName -Raw | ConvertFrom-Json
            $slang = Join-Path $metas[0].DirectoryName $meta.source
            $actualSourceHash = (Get-FileHash -LiteralPath $slang -Algorithm SHA256).Hash.ToLowerInvariant()
            $actualMetaHash = (Get-FileHash -LiteralPath $metas[0].FullName -Algorithm SHA256).Hash
            if ($meta.schema -ne 1 -or $meta.generatedMaterial.adapter -ne 1 -or
                $meta.generatedMaterial.graph -ne '11111111-1111-4111-8111-111111111111' -or
                $meta.generatedMaterial.sourceSha256 -ne $actualSourceHash -or
                @($meta.properties).Count -ne 1 -or $meta.properties[0].parameterId -ne 900 -or
                @($meta.passes).Count -ne 6 -or
                (@($meta.passes.name | Sort-Object) -join ',') -ne 'Forward,GBuffer,LXSceneColor,LXSceneLookup0,LXSceneLookup1,Shadow') {
                throw "Actual $configuration generated pair contract mismatch."
            }
            $forward = @($meta.passes | Where-Object name -EQ 'Forward')[0]
            if ($forward.queue -ne 'transparent' -or $forward.state.blend -ne 'alpha' -or $forward.state.depthWrite) {
                throw "Actual $configuration Forward+ alpha contract mismatch."
            }
            if ($null -ne $metaHash -and ($metaHash -ne $actualMetaHash -or $sourceHash -ne $actualSourceHash)) {
                throw "Actual $configuration repeated cook changed its source pair."
            }
            $metaHash = $actualMetaHash
            $sourceHash = $actualSourceHash
        }
        "MAT7_SHADERMETA_COOKER_OK configuration=$configuration repeated=true sourceDigest=true pairs=1"
        if ($VerifyConsumer) {
            $consumer = Join-Path $repo "Bin/x64-$configuration/Tools/MaterialCommonConsumerProbe/MaterialCommonConsumerProbe.exe"
            if (!(Test-Path -LiteralPath $consumer)) { throw "Build MaterialCommonConsumerProbe $configuration before the consumer gate." }
            $package = Join-Path $cookRoot 'SourceFreePackage/Assets'
            New-Item -ItemType Directory -Path $package -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $cookRoot 'Accepted/Derived') -Destination $package -Recurse
            $previousPath = $env:PATH
            try {
                $dependencyBin = if ($configuration -eq 'Debug') { 'debug/bin' } else { 'bin' }
                $env:PATH = (Join-Path $dependencies $dependencyBin) + ';' + $previousPath
                $consumed = @(& $consumer $package $repo (Join-Path $work 'Assets/fixture.shadergraph') 2>&1)
                $consumeExit = $LASTEXITCODE
            } finally { $env:PATH = $previousPath }
            $consumed | Set-Content -LiteralPath (Join-Path $cookRoot 'consumer.log') -Encoding utf8
            if ($consumeExit -ne 0 -or @($consumed | Where-Object { $_ -match '^MAT7_COMMON_CONSUMER_OK ' }).Count -ne 1 -or
                @($consumed | Where-Object { $_ -match '^MAT7_CODE_RUNTIME_OK ' }).Count -ne 1 -or
                @($consumed | Where-Object { $_ -match '^MAT7_PIPELINE_RUNTIME_OK ' }).Count -ne 1 -or
                @($consumed | Where-Object { $_ -match '^MAT7_COOKED_GRAPHICS_OK ' }).Count -ne 1) {
                throw "Actual $configuration common Material consumer failed: $($consumed -join "`n")"
            }
            $consumed | Where-Object { $_ -match '^MAT7_(COMMON_CONSUMER|CODE_RUNTIME|PIPELINE_RUNTIME|COOKED_GRAPHICS)_OK ' }
        }
    }
}
