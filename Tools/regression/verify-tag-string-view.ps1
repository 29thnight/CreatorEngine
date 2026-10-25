[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC toolchain unavailable' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$output = Join-Path $repo "Build/Tests/TagManagerStringView/x64-$Configuration"
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $msbuild "$PSScriptRoot/TagManagerStringViewTests.vcxproj" /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$repo\" /p:VcpkgManifestInstall=false /nologo /v:minimal *> "$output/build.log"
if ($LASTEXITCODE) { throw "TagManager regression build failed: $output/build.log" }
$previousPath = $env:PATH
try {
    $env:PATH = "$repo/Bin/x64-$Configuration/Runtime/Common;$repo/vcpkg_installed/x64-windows/x64-windows/bin;$repo/ThirdParty/DotNetHost/bin;$previousPath"
    & "$output/TagManagerStringViewTests.exe" *> "$output/test.log"
    if ($LASTEXITCODE) { throw "TagManager regression failed (exit $LASTEXITCODE): $output/test.log" }
} finally { $env:PATH = $previousPath }
Get-Content "$output/test.log"
