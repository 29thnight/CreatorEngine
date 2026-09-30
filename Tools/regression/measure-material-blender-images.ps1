param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Label='matched',
 [string]$Reference='',
 [string]$ReferenceRepeat='',
 [string]$Python='C:\Python313\python.exe'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid measurement label' }
if(!$Reference) { $Reference=Join-Path $repo 'Tools/blender/fixtures/material-matched-5.1.1' }
$Reference=[IO.Path]::GetFullPath($Reference)
$case=Join-Path $repo "Build/Obj/Mat9Images-$Configuration-$Label"
if(Test-Path $case) { throw "Output already exists: $case" }
if(!(Test-Path "$Reference/manifest.json") -or !(Test-Path "$Reference/sphere.bin")) { throw 'Matched reference missing' }
& $Python "$PSScriptRoot/compare-material-blender-images.py" $Reference --validate-reference
if($LASTEXITCODE -ne 0) { throw 'Matched reference identity failed' }
New-Item -ItemType Directory $case | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
& $msbuild "$PSScriptRoot/MaterialMatchedImageProbe.vcxproj" /m:2 /nologo "/p:Configuration=$Configuration" /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false /p:VcpkgManifestInstall=false /v:minimal *> "$case/build.log"
if($LASTEXITCODE -ne 0) { throw "Native image probe build failed; see $case/build.log" }
$previousPath=$env:PATH
$previousValidation=$env:CREATOR_DX12_VALIDATION
try {
 $dependency=if($Configuration -eq 'Debug'){'vcpkg_installed/x64-windows/debug/bin'}else{'vcpkg_installed/x64-windows/bin'}
 $env:PATH=(Join-Path $repo $dependency)+';'+$previousPath
 $env:CREATOR_DX12_VALIDATION='gpu'
 & "$repo/Bin/x64-$Configuration/Tools/MaterialMatchedImageProbe/MaterialMatchedImageProbe.exe" $repo $Reference "$case/Native" *> "$case/native.log"
 if($LASTEXITCODE -ne 0) { throw "Native image capture failed; see $case/native.log" }
} finally {
 $env:PATH=$previousPath
 $env:CREATOR_DX12_VALIDATION=$previousValidation
}
$comparisonArguments=@("$PSScriptRoot/compare-material-blender-images.py",$Reference,"$case/Native","$case/comparison.json")
if($ReferenceRepeat) { $comparisonArguments+=@('--reference-repeat',[IO.Path]::GetFullPath($ReferenceRepeat)) }
& $Python @comparisonArguments | Tee-Object -FilePath "$case/comparison.log"
if($LASTEXITCODE -ne 0) { throw 'Matched emission/input control failed' }
Write-Output "MAT9_IMAGE_MEASUREMENT_OK configuration=$Configuration materialAcceptance=pending output=$case"
