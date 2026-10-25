[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# Producer wiring is checked separately from the executable visibility contract.
# These paths must seal the matrix before publishing a pose to the const view.
$component = [IO.File]::ReadAllText((Join-Path $repo 'Engine/SceneRuntime/FoliageComponent.cpp'))
$header = [IO.File]::ReadAllText((Join-Path $repo 'Engine/SceneRuntime/FoliageComponent.h'))
$required = @(
    'sealed\.RebuildWorldMatrix\(\);\s*m_foliageInstances\.push_back',
    'found->m_scale = scale;\s*found->RebuildWorldMatrix\(\);',
    'found->RebuildWorldMatrix\(\);\s*m_worldBoundsCache\.Invalidate\(',
    'binding\.status = Status::Ready;[\s\S]*?m_worldBoundsCache\.InvalidateType\(',
    'm_foliageInstances\.clear\(\);\s*m_worldBoundsCache\.Clear\(\);',
    'm_foliageTypes\.erase\([^;]+;\s*m_worldBoundsCache\.InvalidateAll\(\);',
    'm_foliageInstances\.erase\(m_foliageInstances\.begin\(\)\+index\);\s*m_worldBoundsCache\.InvalidateAll\(\);',
    'm_foliageInstances\.size\(\) != previousSize\)\s*\{\s*m_worldBoundsCache\.InvalidateAll\(\);',
    'Meta::Deserialize\(&instance, instanceNode\);\s*AddFoliageInstance\(instance\);',
    'inst\.m_position\.y = heightMap\[idx\];\s*AddFoliageInstance\(inst\);',
    'm_worldBoundsCache\.Cull\(std::span\{m_foliageInstances\}'
)
foreach ($pattern in $required) {
    if ($component -notmatch $pattern) { throw "Foliage sealed-pose producer wiring changed: $pattern" }
}
if ($header -notmatch '\[\[reflgen::ignore\]\]\s*foliage::WorldBoundsCache m_worldBoundsCache') {
    throw 'World bounds cache must remain nonserialized component-local state'
}
if ($header -notmatch 'const std::vector<FoliageInstance>& GetFoliageInstances\(\) const') {
    throw 'Foliage instance publication must remain a const view'
}
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC toolchain unavailable' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$output = Join-Path $repo "Build/Tests/FoliageCulling/x64-$Configuration"
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $msbuild "$PSScriptRoot/FoliageCullingTests.vcxproj" /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$repo\" /p:VcpkgManifestInstall=false /nologo /v:minimal *> "$output/build.log"
if ($LASTEXITCODE) { throw "Foliage culling regression build failed: $output/build.log" }
& "$output/FoliageCullingTests.exe" *> "$output/test.log"
if ($LASTEXITCODE) { throw "Foliage culling regression failed: $output/test.log" }
Get-Content "$output/test.log"
