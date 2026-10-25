[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC toolchain unavailable' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$output = Join-Path $repo "Build/Tests/RHICompletionRetireQueue/x64-$Configuration"
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $msbuild "$PSScriptRoot/RHICompletionRetireQueueTests.vcxproj" /m:4 "/p:Configuration=$Configuration" /p:Platform=x64 "/p:SolutionDir=$repo\" /p:VcpkgManifestInstall=false /nologo /v:minimal *> "$output/build.log"
if ($LASTEXITCODE) { throw "Retire queue regression build failed: $output/build.log" }
& "$output/RHICompletionRetireQueueTests.exe" *> "$output/test.log"
if ($LASTEXITCODE) { throw "Retire queue regression failed: $output/test.log" }
Get-Content "$output/test.log"
