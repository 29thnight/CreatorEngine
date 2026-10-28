param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $output|Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)[0]
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$runs=@()
foreach($configuration in @('Debug','Release')){
    & $msbuild (Join-Path $PSScriptRoot 'EditorMeshPsoProbe.vcxproj') /t:Build /m:4 /nologo /v:minimal `
        "/p:Configuration=$configuration" /p:Platform=x64 /p:EngineEffectPackage=true `
        /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" *> "$output/build-$configuration.log"
    if($LASTEXITCODE){throw "Native PSO build failed: $configuration"}
    $exe=Join-Path $repo "Bin/x64-$configuration/Tools/EditorMeshPsoProbe/EditorMeshPsoProbe.exe"
    $oldPath=$env:PATH; $oldValidation=$env:CREATOR_DX12_VALIDATION
    try{
        $package=if($configuration -eq 'Debug'){'debug/bin'}else{'bin'}
        $env:PATH="$repo/Bin/x64-$configuration/Runtime/Common;$repo/vcpkg_installed/x64-windows/x64-windows/$package;$repo/ThirdParty/DotNetHost/bin;$oldPath"
        $env:CREATOR_DX12_VALIDATION='gpu'
        & $exe $repo "$output/$configuration" *> "$output/run-$configuration.log"
        $exitCode=$LASTEXITCODE
    }finally{$env:PATH=$oldPath; $env:CREATOR_DX12_VALIDATION=$oldValidation}
    $marker=@(Get-Content "$output/run-$configuration.log"|Where-Object {$_ -match '^EDITOR_MESH_PSO_OK '})
    $expected=if($configuration -eq 'Debug'){'allocationChecks=2 nodeFreeChecks=1$'}else{'allocationChecks=0 nodeFreeChecks=0$'}
    if($exitCode -ne 0 -or $marker.Count -ne 1 -or $marker[0] -notmatch $expected){throw "Native PSO execution failed: $configuration; see run log"}
    $runs+= [ordered]@{configuration=$configuration;exitCode=$exitCode;binarySHA256=(Get-FileHash $exe).Hash;verdict=$marker[0]}
    Write-Output "$configuration $($marker[0])"
}
$paths=@('Engine/RenderEngine/RHI/DX12/DX12PSOManager.h','Engine/RenderEngine/RHI/DX12/DX12PSOManager.cpp',
    'Tools/regression/editor_mesh_pso_probe.cpp','Tools/regression/verify-pso-container-migration.ps1')
[ordered]@{pass=$true;runs=$runs;sources=@($paths|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}})}|
    ConvertTo-Json -Depth 8|Set-Content "$output/result.json" -Encoding utf8
