[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'All',
    [switch]$CompileProduct,
    [string]$VisualStudioInstallation = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = Join-Path $VisualStudioInstallation 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing x64 toolchain: $vcvars" }
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($current in $configurations) {
    $output = Join-Path $repoRoot "Build/Obj/AnimationPlayback/$current"
    New-Item -Path $output -ItemType Directory -Force | Out-Null
    $probe = Join-Path $PSScriptRoot 'animation_playback_probe.cpp'
    $executable = Join-Path $output 'animation_playback_probe.exe'
    $options = if ($current -eq 'Debug') { '/MDd /Od /RTC1' } else { '/MD /O2' }
    $command = 'call "' + $vcvars + '" >nul && cl.exe /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        $options + ' /I"' + (Join-Path $repoRoot 'Engine/SceneRuntime') +
        '" /external:I"' + (Join-Path $repoRoot 'ThirdParty/Mathematics/include') +
        '" /external:W0 /Fo:"' + (Join-Path $output 'probe.obj') +
        '" /Fd:"' + (Join-Path $output 'probe.pdb') + '" /Fe:"' + $executable + '" "' + $probe + '"'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "$current animation probe compilation failed: $LASTEXITCODE" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "$current animation playback regression failed: $LASTEXITCODE" }
    Write-Output "$current animation playback regression passed."

    if ($CompileProduct) {
        $msbuild = Join-Path $VisualStudioInstallation 'MSBuild/Current/Bin/MSBuild.exe'
        $compileOutput = Join-Path $repoRoot "Build/Obj/Phase13S0/Selected-$current/"
        New-Item -Path $compileOutput -ItemType Directory -Force | Out-Null
        foreach ($file in @('AnimationJob.cpp', 'AnimationEventBridge.cpp',
            'AnimationController.cpp', 'Animator.cpp')) {
            # MSVC's SelectClCompile compares one item identity. Passing a list
            # here would select nothing and compile the entire non-unity project.
            $compileLog = Join-Path $compileOutput "$file.log"
            & $msbuild (Join-Path $repoRoot 'Engine/SceneRuntime/SceneRuntime.vcxproj') `
                '/t:ClCompile' "/p:Configuration=$current" '/p:Platform=x64' `
                "/p:SolutionDir=$repoRoot/" '/p:BuildProjectReferences=false' `
                '/p:EnableUnitySupport=false' "/p:SelectedFiles=$file" `
                "/p:IntDir=$compileOutput" '/m:2' '/nologo' '/v:quiet' `
                "/flp:logfile=$compileLog;verbosity=normal"
            if ($LASTEXITCODE -ne 0) {
                throw "$current $file compile failed ($LASTEXITCODE): $compileLog"
            }
            Write-Output "COMPILE_OK $current $file"
        }
    }
}
