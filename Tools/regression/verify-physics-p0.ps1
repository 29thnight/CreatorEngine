[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$dependencies = Join-Path $repo 'vcpkg_installed/x64-windows/x64-windows'
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$out = Join-Path $repo 'Build/Obj/Phase19P0'
New-Item -ItemType Directory -Force $out | Out-Null
$configurations=if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)}
foreach ($configuration in $configurations) {
    $folder=Join-Path $out $configuration
    New-Item -ItemType Directory -Force $folder | Out-Null
    $library=if($configuration -eq 'Debug'){Join-Path $dependencies 'debug'}else{$dependencies}
    $flags=if($configuration -eq 'Debug'){'/MDd /Od /D_DEBUG'}else{'/MD /O2 /DNDEBUG'}
    foreach($kind in @('cpp23','cpu')) {
        $exe=Join-Path $folder ($kind+'.exe')
        $source=Join-Path $PSScriptRoot ('physics_p0_'+$kind+'_probe.cpp')
        $command='call "'+$vcvars+'" >nul && cl /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /W4 '+$flags+
            ' /I"'+(Join-Path $repo 'ThirdParty/Mathematics/include')+'" /external:I"'+(Join-Path $dependencies 'include')+
            '" /external:W0 /Fo"'+$folder+'/" /Fe"'+$exe+'" "'+$source+'"'
        if($kind -eq 'cpu') {
            $command+=' /external:I"'+(Join-Path $dependencies 'include/physx')+'"'
            $command+=' /link /LIBPATH:"'+(Join-Path $library 'lib')+'" PhysX_64.lib PhysXCommon_64.lib PhysXFoundation_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib'
            foreach($dll in @('PhysX_64.dll','PhysXCommon_64.dll','PhysXFoundation_64.dll')) {
                Copy-Item -LiteralPath (Join-Path $library ('bin/'+$dll)) -Destination $folder -Force
            }
        }
        & $env:ComSpec /d /s /c $command
        if($LASTEXITCODE -ne 0){throw "$configuration $kind compilation failed"}
        $process=Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder ($kind+'.jsonl')) -RedirectStandardError (Join-Path $folder ($kind+'.err'))
        $handle=$process.Handle
        if(-not $process.WaitForExit(120000)){$process.Kill();$process.WaitForExit();throw "$configuration $kind timeout"}
        $process.WaitForExit()
        if($process.ExitCode -ne 0){throw "$configuration $kind failed ($($process.ExitCode)): $(Get-Content (Join-Path $folder ($kind+'.err')) -Raw)"}
        $records=@(Get-Content (Join-Path $folder ($kind+'.jsonl')) | ForEach-Object {$_ | ConvertFrom-Json})
        if($kind -eq 'cpp23' -and ($records.Count -ne 1 -or $records[0].result -ne 'CPP23_OK')){throw 'Invalid feature result'}
        if($kind -eq 'cpu' -and $records.Count -ne 9){throw 'Missing CPU baseline cases'}
        Write-Output "PHYSICS_P0_OK $configuration $kind records=$($records.Count)"
    }
}
