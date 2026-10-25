[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& python (Join-Path $repo "Tools/build/generate-module-adapters.py") --check
if ($LASTEXITCODE -ne 0) { throw "Module adapters are stale" }
& python (Join-Path $PSScriptRoot "verify-delegate-jobs-modules.py")
if ($LASTEXITCODE -ne 0) { throw "Delegate/Jobs module boundary failed" }
. (Join-Path $PSScriptRoot 'Resolve-EnkiDependencies.ps1')
$dependencies = Resolve-EnkiDependencies $repo
$msbuild = 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
function Invoke-Probe([string]$Executable) {
    $stdout = "$Executable.out"
    $stderr = "$Executable.err"
    $process = Start-Process -FilePath $Executable -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    if (-not $process.WaitForExit(45000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Probe timed out: $Executable"
    }
    $process.WaitForExit()
    Get-Content -LiteralPath $stdout
    if ($process.ExitCode -ne 0) {
        throw "Probe failed (exit=$($process.ExitCode)): $(Get-Content -LiteralPath $stderr -Raw)"
    }
}
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }
foreach ($config in $configs) {
    foreach ($name in @('DelegateModuleProbe','JobsModuleProbe')) {
        & $msbuild (Join-Path $PSScriptRoot "$name.vcxproj") /t:Build "/p:Configuration=$config" /p:Platform=x64 "/p:SolutionDir=$repo\" "/p:CreatorEnkiDependencies=$dependencies" /m:2 /nr:false /nologo /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "$name $config build failed" }
        $out = Join-Path $repo "Build/Validation/UtilityModules/$name/x64-$config"
        if ($name -eq 'JobsModuleProbe') {
            $provider = if ($config -eq 'Debug') { Join-Path $dependencies 'debug/bin/enkiTS.dll' } else { Join-Path $dependencies 'bin/enkiTS.dll' }
            Copy-Item -LiteralPath $provider -Destination $out
        }
        Invoke-Probe (Join-Path $out "$name.exe")
    }
    $out = Join-Path $repo "Build/Validation/UtilityModules/DelegateStandaloneProbe/x64-$config"
    New-Item -ItemType Directory -Force $out | Out-Null
    $flags = if ($config -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $exe = Join-Path $out 'delegate.exe'
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /Zi /DCE_CONTRACT_STANDALONE ' + $flags + ' /Fo"' + $out + '/" /Fd"' + $out + '/delegate.pdb" /Fe"' + $exe + '" "' + (Join-Path $PSScriptRoot 'delegate_module_probe.cpp') + '" /link /DEBUG'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Delegate standalone $config compile failed" }
    Invoke-Probe $exe
}
