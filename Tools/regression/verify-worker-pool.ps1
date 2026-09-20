[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }
foreach ($config in $configs) {
    $out = Join-Path $repo "Build/Obj/Phase13Jobs/Pool-$config"
    New-Item -ItemType Directory -Force $out | Out-Null
    $lib = Join-Path $repo $(if ($config -eq 'Debug') {'vcpkg_installed/x64-windows/debug'} else {'vcpkg_installed/x64-windows'})
    Copy-Item -LiteralPath (Join-Path $lib 'bin/enkiTS.dll') -Destination $out
    foreach ($mutant in @($false,$true)) {
        $name = if ($mutant) {'missing-barrier'} else {'worker-pool'}
        $source = Join-Path $repo 'Engine/Utility_Framework/JobScheduler.cpp'
        if ($mutant) {
            $text = [IO.File]::ReadAllText($source)
            $old = 'state_->done_.wait(lock, [this] { return state_->complete_; });'
            if (-not $text.Contains($old)) { throw 'Barrier mutation target missing' }
            $source = Join-Path $out 'MissingBarrier.cpp'
            [IO.File]::WriteAllText($source, $text.Replace($old, '{}'))
        }
        $exe = Join-Path $out "$name.exe"
        $flags = if ($config -eq 'Debug') {'/MDd /Od /RTC1 /D_DEBUG'} else {'/MD /O2 /DNDEBUG'}
        $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /DENKITS_DLL=1 ' +
            $flags + ' /I"' + (Join-Path $repo 'Engine/Utility_Framework') + '" /external:I"' +
            (Join-Path $repo 'vcpkg_installed/x64-windows/include') + '" /external:W0 /Fo"' + $out +
            '/" /Fd"' + (Join-Path $out "$name.pdb") + '" /Fe"' + $exe + '" "' + $source +
            '" "' + (Join-Path $repo 'Engine/Utility_Framework/ThreadPool.cpp') + '" "' + (Join-Path $PSScriptRoot 'worker_pool_probe.cpp') + '" /link /LIBPATH:"' + (Join-Path $lib 'lib') + '"'
        & $env:ComSpec /d /s /c $command
        if ($LASTEXITCODE -ne 0) { throw "$config $name compile failed" }
        $proc = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $out "$name.out") -RedirectStandardError (Join-Path $out "$name.err")
        $processHandle = $proc.Handle
        if (-not $proc.WaitForExit(45000)) { $proc.Kill(); $proc.WaitForExit(); throw "$config $name timed out" }
        $proc.WaitForExit() # also drain redirected output before reading it
        $stdout = Get-Content (Join-Path $out "$name.out") -Raw
        $stderr = Get-Content (Join-Path $out "$name.err") -Raw
        if ($mutant) {
            if ($proc.ExitCode -ne 1 -or $stderr -notmatch 'barrier returned before independent completion') {
                throw "Barrier mutant was not caught as expected: $stderr"
            }
            Write-Output "WORKER_POOL_MUTATION_OK $config"
        } else {
            if ($proc.ExitCode -ne 0 -or $stdout -notmatch 'JOB_SCHEDULER_OK') { throw "$config pool failed (exit=$($proc.ExitCode)): $stderr" }
            Write-Output "$config $stdout"
        }
    }
}
