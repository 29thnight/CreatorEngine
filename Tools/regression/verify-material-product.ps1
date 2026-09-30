param(
    [string]$VisualStudioInstallation = '',
    [switch]$VerifyAssetCooker
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioInstallation = @(& $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
}
$vcvars = Join-Path $VisualStudioInstallation 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $repo 'Build\Obj\MaterialProductProbe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exe = Join-Path $output 'material_product_probe.exe'
$sources = @('Tools/regression/material_product_probe.cpp', 'Engine/RenderEngine/MaterialGraphProduct.cpp',
    'Engine/RenderEngine/RHI/RHIShaderCompiler.cpp', 'Engine/RenderEngine/RHI/RHIShaderSource.cpp',
    'Engine/RenderEngine/RHI/RHIShaderReflection.cpp', 'Engine/RenderEngine/RHI/RHIShaderPermutation.cpp',
    'Lattice/Core/LXGraph.cpp', 'Lattice/Core/LXNodeDefinition.cpp', 'Lattice/Material/LXMaterialGraph.cpp',
    'Lattice/Material/LXMaterialIR.cpp', 'Lattice/Material/LXMaterialNodes.cpp',
    'Lattice/Material/LXMaterialOperators.cpp', 'Lattice/Material/LXMaterialCompiler.cpp')
$sources += @('Engine/RenderEngine/Experiment/Cooked/CookedMaterialProgram.cpp',
    'Engine/RenderEngine/MaterialGraphRuntime.cpp', 'Tools/regression/material_runtime_tests.cpp',
    'Engine/Utility_Framework/AuthoringScalarConvert.cpp', 'Engine/Utility_Framework/AuthoringRymlErrorPolicy.cpp',
    'Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.cpp',
    'Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.cpp',
    'Engine/RenderEngine/Assets/AssetIdentityProfile.cpp')
$quoted = ($sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }) -join ' '
$compile = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest ' +
    '/permissive- /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /W4 /WX /MD ' +
    '/I"' + (Join-Path $repo 'ThirdParty/Slang/include') + '" /I"' +
    (Join-Path $repo 'Engine/Utility_Framework') + '" /I"' +
    (Join-Path $repo 'ThirdParty/Mathematics/include') + '" /I"' +
    (Join-Path $repo 'vcpkg_installed/x64-windows/include') + '" /Fo:"' + $output + '/" /Fe:"' + $exe + '" ' +
    $quoted + ' /link /LIBPATH:"' + (Join-Path $repo 'vcpkg_installed/x64-windows/lib') +
    '" ryml.lib c4core.lib d3d12.lib dxgi.lib ole32.lib'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw "Material product probe build failed: exit $LASTEXITCODE" }
$result = @(& $exe $repo 2>&1)
$result | Set-Content -LiteralPath (Join-Path $output 'product.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or @($result | Where-Object { $_ -match '^LX_MATERIAL_PRODUCT_OK ' }).Count -ne 1 -or
    @($result | Where-Object { $_ -match '^LX_MATERIAL_RUNTIME_OK ' }).Count -ne 1) {
    throw "Material product gate failed: $($result -join "`n")"
}
$result

if ($VerifyAssetCooker) {
    $programPath = 'Derived/MaterialPrograms/11/11111111-1111-4111-8111-111111111111.lxmaterial'
    foreach ($configuration in @('Debug', 'Release')) {
        $cooker = Join-Path $repo "Bin/x64-$configuration/Tools/AssetCooker/AssetCooker.exe"
        if (!(Test-Path -LiteralPath $cooker -PathType Leaf)) {
            throw "Build AssetCooker $configuration before running the product cook gate."
        }
        $caseRoot = Join-Path $output ("CookGate-$configuration-" + [Guid]::NewGuid().ToString('N'))
        $assetRoot = Join-Path $caseRoot 'Assets'
        New-Item -ItemType Directory -Path $assetRoot | Out-Null
        $graphFile = Join-Path $assetRoot 'fixture.shadergraph'
        Copy-Item -LiteralPath (Join-Path $output 'fixture.shadergraph') -Destination $graphFile
        Copy-Item -LiteralPath (Join-Path $output 'fixture.shadergraph.meta') -Destination "$graphFile.meta"
        [IO.File]::WriteAllBytes((Join-Path $assetRoot 'fixture.png'), [Convert]::FromBase64String(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jVI4AAAAASUVORK5CYII='))
        'guid: 22222222-2222-4222-8222-222222222222' |
            Set-Content -LiteralPath (Join-Path $assetRoot 'fixture.png.meta') -Encoding utf8
        $acceptedRoot = Join-Path $caseRoot 'Accepted'
        $arguments = @('--asset-root', $assetRoot, '--output', $acceptedRoot,
            '--shadergraph', 'fixture.shadergraph', '--material-program-root', $output,
            '--texture', (Join-Path $assetRoot 'fixture.png'))
        $cookOutput = @(& $cooker @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        $cookOutput | Set-Content -LiteralPath (Join-Path $caseRoot 'accepted.log') -Encoding utf8
        if ($exitCode -ne 0 -or !($cookOutput -match 'materialPrograms=1')) {
            throw "Actual $configuration AssetCooker failed: $($cookOutput -join "`n")"
        }
        $consumer = @(& $exe --cooked-root $acceptedRoot 2>&1)
        $exitCode = $LASTEXITCODE
        $consumer | Set-Content -LiteralPath (Join-Path $caseRoot 'consumer.log') -Encoding utf8
        if ($exitCode -ne 0 -or !($consumer -match '^LX_MATERIAL_COOKED_PRODUCT_OK ')) {
            throw "Actual $configuration cooked consumer failed: $($consumer -join "`n")"
        }
        $consumer
        $acceptedRoot | Set-Content -LiteralPath (Join-Path $output "runtime-cooked-$configuration-root.txt") -Encoding utf8
        $acceptedHash = (Get-FileHash -LiteralPath (Join-Path $acceptedRoot $programPath) -Algorithm SHA256).Hash
        $manifestHash = (Get-FileHash -LiteralPath (Join-Path $acceptedRoot 'Derived/asset-manifest.cemf') -Algorithm SHA256).Hash
        $sourceText = [IO.File]::ReadAllText($graphFile)
        $changedGraph = $sourceText | ConvertFrom-Json
        $changedGraph.blackboard[0].default.value = 0.23
        $badPrograms = Join-Path $caseRoot 'BadPrograms'
        $badProgramFile = Join-Path $badPrograms $programPath
        New-Item -ItemType Directory -Path (Split-Path -Parent $badProgramFile) -Force | Out-Null
        $corrupt = [IO.File]::ReadAllBytes((Join-Path $output $programPath))
        $corrupt[0] = $corrupt[0] -bxor 1
        [IO.File]::WriteAllBytes($badProgramFile, $corrupt)
        foreach ($failureCase in @('stale-graph', 'missing-texture', 'corrupt-program')) {
            $rejectedRoot = Join-Path $caseRoot $failureCase
            $failureArguments = @('--asset-root', $assetRoot, '--output', $rejectedRoot,
                '--shadergraph', 'fixture.shadergraph', '--material-program-root')
            if ($failureCase -eq 'corrupt-program') {
                $failureArguments += $badPrograms
            } else {
                $failureArguments += $output
            }
            if ($failureCase -ne 'missing-texture') {
                $failureArguments += @('--texture', (Join-Path $assetRoot 'fixture.png'))
            }
            try {
                if ($failureCase -eq 'stale-graph') {
                    [IO.File]::WriteAllText($graphFile, ($changedGraph | ConvertTo-Json -Depth 100))
                }
                $failureOutput = @(& $cooker @failureArguments 2>&1)
                $exitCode = $LASTEXITCODE
            } finally {
                [IO.File]::WriteAllText($graphFile, $sourceText)
            }
            $failureOutput | Set-Content -LiteralPath (Join-Path $caseRoot "$failureCase.log") -Encoding utf8
            $expected = switch ($failureCase) {
                'stale-graph' { 'differs from the current source graph' }
                'missing-texture' { 'dependency' }
                'corrupt-program' { 'Corrupt, incompatible or oversized' }
            }
            if ($exitCode -eq 0 -or !($failureOutput -match $expected) -or
                (Test-Path -LiteralPath $rejectedRoot) -or
                (Get-FileHash -LiteralPath (Join-Path $acceptedRoot $programPath) -Algorithm SHA256).Hash -ne $acceptedHash -or
                (Get-FileHash -LiteralPath (Join-Path $acceptedRoot 'Derived/asset-manifest.cemf') -Algorithm SHA256).Hash -ne $manifestHash) {
                throw "Cook failure publication gate failed: $configuration/$failureCase"
            }
        }
        "LX_MATERIAL_COOKER_OK configuration=$configuration rejected=3 acceptedBytesPreserved=true"
    }
}
exit 0
