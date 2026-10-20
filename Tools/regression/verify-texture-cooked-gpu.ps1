param([ValidateSet('dx12','vulkan','All')][string]$Backend = 'dx12', [switch]$SkipBuild)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repo ('Build/Obj/TextureCookedGpu/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$backends = if ($Backend -eq 'All') { @('dx12','vulkan') } else { @($Backend) }
$paths = @('Tools/regression/texture_cooked_gpu_fixture.cpp','Tools/regression/texture_cooked_gpu_probe.cpp',
    'Tools/regression/texture_cooked_gpu_probe.slang','Tools/regression/TextureCookedGpuFixture.vcxproj',
    'Tools/regression/TextureCookedGpuProbe.vcxproj','Tools/regression/verify-texture-cooked-gpu.ps1',
    'Engine/RenderEngine/DataSystem.cpp',
    'Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp','Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h',
    'Engine/RenderEngine/Experiment/MaterialResolver.cpp','Engine/RenderEngine/Experiment/Cooked/CookedTexture.cpp',
    'Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp','Engine/RenderEngine/RHI/DX12/DX12TextureCache.cpp',
    'Engine/RenderEngine/RHI/RHIShaderCompiler.cpp', 'Tools/regression/TextureCookedVulkanProbe.vcxproj',
    'Engine/RenderEngine/RHI/Vulkan/VulkanRenderServices.cpp','Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h',
    'Engine/RenderEngine/RHI/Vulkan/VulkanDeviceResources.cpp','Engine/RenderEngine/RHI/Vulkan/VulkanCommandBufferPool.cpp')
$sources = @($paths | ForEach-Object { $path = Join-Path $repo $_; [pscustomobject]@{path=$path; hash=(Get-FileHash $path).Hash} })
if (!$SkipBuild) {
    $msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
    & $msbuild (Join-Path $repo 'Engine/RenderEngine/RenderEngine.vcxproj') /t:Build /p:Configuration=Release /p:Platform=x64 `
        /m:2 /nologo /v:minimal *> (Join-Path $root 'RenderEngine-build.log')
    if ($LASTEXITCODE -ne 0) { throw "Current RenderEngine build failed: $root" }
    $projects = @('TextureCookedGpuFixture') + @($backends | ForEach-Object {
        if ($_ -eq 'dx12') { 'TextureCookedGpuProbe' } else { 'TextureCookedVulkanProbe' }
    })
    foreach ($project in $projects) {
        & $msbuild (Join-Path $PSScriptRoot ($project + '.vcxproj')) /t:Build /p:Configuration=Release /p:Platform=x64 `
            /m:2 /nologo /v:minimal *> (Join-Path $root ($project + '-build.log'))
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $project, $root" }
    }
}
$fixture = Join-Path $repo 'Bin/x64-Release/Tools/TextureCookedGpuFixture/TextureCookedGpuFixture.exe'
$receipts = @()
$oldPath = $env:PATH
$oldValidation = $env:CREATOR_DX12_VALIDATION
try {
    $env:PATH = (Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows/bin') + ';' +
        (Join-Path $repo 'Bin/x64-Release/Runtime/Common') + ';' + $oldPath
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    & $fixture $root *> (Join-Path $root 'fixture.log')
    if ($LASTEXITCODE -ne 0) { throw "Offline fixture cook failed: $root" }
    $fixtureFiles = @(Get-ChildItem $root -File | Where-Object Extension -In @('.cetex','.reference') | ForEach-Object {
        [pscustomobject]@{path=$_.FullName; hash=(Get-FileHash $_.FullName).Hash}
    })
    foreach ($currentBackend in $backends) {
    $name = if ($currentBackend -eq 'dx12') { 'TextureCookedGpuProbe' } else { 'TextureCookedVulkanProbe' }
    $probe = Join-Path $repo "Bin/x64-Release/Tools/$name/$name.exe"
    $probeLog = Join-Path $root ($currentBackend + '-probe.log')
    & $probe $root (Join-Path $PSScriptRoot 'texture_cooked_gpu_probe.slang') *> $probeLog
    $exitCode = $LASTEXITCODE
    $log = Get-Content $probeLog -Raw
    $validationMarker = if ($currentBackend -eq 'dx12') { 'GPUValidation=on' } else { 'VULKAN_VALIDATION_ENABLED=true' }
    if ($exitCode -ne 0 -or $log -notmatch "TEXTURE_COOKED_GPU_OK backend=$currentBackend formats=3 components=1020 corruptRejected=3 validation=0" -or
        $log -notmatch $validationMarker -or
        $log -notmatch 'TEXTURE_IMAGE_IDENTITY_OK variants=48 partitions=8 pathIgnored=1' -or
        [regex]::Matches($log, 'TEXTURE_PREPARED_BINDING_OK cases=6 unmountedOwner=1').Count -ne 3) {
        throw "Cooked GPU verification failed: backend=$currentBackend exit=$exitCode $root"
    }
    # Ensure a wrong reference cannot pass the GPU comparison.
    $negative = Join-Path $root ($currentBackend + '-OracleNegative')
    New-Item -ItemType Directory -Path $negative | Out-Null
    foreach ($entry in $fixtureFiles) { Copy-Item -LiteralPath $entry.path -Destination $negative }
    $reference = Join-Path $negative 'bc5.reference'
    $bad = [IO.File]::ReadAllBytes($reference)
    [Array]::Copy([BitConverter]::GetBytes([float]1.0), 0, $bad, 0, 4)
    [IO.File]::WriteAllBytes($reference, $bad)
    $negativeLog = Join-Path $root ($currentBackend + '-oracle-negative.log')
    & $probe $negative (Join-Path $PSScriptRoot 'texture_cooked_gpu_probe.slang') *> $negativeLog
    $oracleNegativeExit = $LASTEXITCODE
    if ($oracleNegativeExit -ne 1 -or (Get-Content $negativeLog -Raw) -notmatch
        'GPU decoded texel mismatch: bc5 mip=0 x=0 y=0 component=0.*expected=1.000000') {
        throw "Wrong GPU reference was not rejected: $root"
    }
    $receipts += [pscustomobject]@{ backend=$currentBackend; exitCode=$exitCode; validation=0; gpuComponents=1020;
        corruptRejected=3; imageIdentityVariants=48; imageIdentityPartitions=8; preparedBindingCases=18;
        oracleNegativeExit=$oracleNegativeExit; binary=$probe; binarySha256=(Get-FileHash $probe).Hash;
        log=$probeLog; negativeLog=$negativeLog }
    }
} finally {
    $env:PATH = $oldPath
    $env:CREATOR_DX12_VALIDATION = $oldValidation
}
foreach ($entry in @($sources) + @($fixtureFiles)) {
    if ((Get-FileHash $entry.path).Hash -ne $entry.hash) { throw "Verification input changed: $($entry.path)" }
}
[ordered]@{configuration='Release'; backend=$Backend; receipts=$receipts; root=$root; exitCode=$exitCode; sourceDrift=0; sources=$sources;
    fixtureFiles=$fixtureFiles; fixtureBinarySha256=(Get-FileHash $fixture).Hash;
    renderEngineLibrarySha256=(Get-FileHash (Join-Path $repo 'Build/Lib/x64-Release/RenderEngine.lib')).Hash;
    formats=@('BC5Unorm','BC7Unorm','BC7UnormSrgb'); mips=4; gpuComponents=(1020*$receipts.Count); corruptRejected=(3*$receipts.Count);
    reference='DirectXTex decompressed cooked blocks; manual sRGB transfer';
    tolerance=@{BC5='1/65535 + 0.03*block endpoint span; endpoints/constants float epsilon1e-7'; BC7=0.0000001;
        BC7Srgb='0.5 ULP on unrounded 8-bit sRGB side; alpha/endpoints float epsilon1e-7'};
    boundary='in-memory cooked AssetSet typed acquisition -> exact image -> product backend texture cache -> SampleLevel/readback; no original image';
    shippingPlayer=$false; modelRoles=$false; vulkan=($backends -contains 'vulkan'); validation=0} |
    ConvertTo-Json -Depth 7 | Set-Content (Join-Path $root 'result.json') -Encoding utf8
$root | Set-Content (Join-Path $repo 'Build/Phase12CoreVerification/cooked-gpu-root.txt') -Encoding utf8
foreach ($receipt in $receipts) {
    Get-Content $receipt.log | Where-Object { $_ -match '(INTEGRITY_OK|GPU_SAMPLE_OK|TEXTURE_COOKED_GPU_OK)' }
}
"TEXTURE_COOKED_GPU_RESULT=$root"
exit 0
