#Requires -Version 7.0
param([ValidateSet('Debug','Release')][string[]]$Configurations=@('Debug','Release'),
    [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
& "$PSScriptRoot/Apply-Patches.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh build evidence directory.'}
New-Item -ItemType Directory $out | Out-Null
$vs='C:/Program Files/Microsoft Visual Studio/18/Community'
$cmake="$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
& "$vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch x64 -HostArch x64 -NoLogo -SkipAutomaticLocation
Push-Location $repo
try {
    foreach($config in $Configurations) {
        & $cmake --build "$PSScriptRoot/FidelityFX-Build" --config $config `
            --target ffx_fsr3upscaler_x64 ffx_opticalflow_x64 ffx_frameinterpolation_x64 ffx_backend_dx12_x64 `
            --parallel 4 -- /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false `
            *> "$out/sdk-$config.log"
        if($LASTEXITCODE){throw "$config FidelityFX build failed."}
        & "$vs/MSBuild/Current/Bin/MSBuild.exe" Editor/CreatorEditor.vcxproj /m:4 /nr:false /t:Build `
            "/p:Configuration=$config" /p:Platform=x64 /p:DotNetHostPackVersion=10.0.11 `
            "/p:ForceImportAfterCppProps=$PSScriptRoot/Validation.props" /nologo '/clp:ErrorsOnly;Summary' `
            "/flp:logfile=$out/build-$config.log;verbosity=minimal"
        if($LASTEXITCODE){throw "$config SDK build failed."}
        $editor="$repo/Bin/x64-$config/Editor"
        Copy-Item "$PSScriptRoot/StreamlineRuntime/bin/x64/*.dll" "$editor/Temporal" -Force
        Copy-Item "$PSScriptRoot/XeSS/bin/*.dll" "$editor/Temporal" -Force
        Copy-Item "$PSScriptRoot/FidelityFX-SDK/sdk/libs/pix/bin/x64/WinPixEventRuntime.dll" $editor -Force
        @{configuration=$config;binarySha256=(Get-FileHash "$editor/CreatorEditor.runtime.dll").Hash} |
            ConvertTo-Json | Set-Content "$out/binary-$config.json"
    }
} finally {
    Pop-Location
}
