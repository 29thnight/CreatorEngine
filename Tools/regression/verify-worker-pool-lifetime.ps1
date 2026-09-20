[CmdletBinding()]
param([Parameter(Mandatory)][string]$EnkiSourceDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$sourceDir = (Resolve-Path -LiteralPath $EnkiSourceDirectory).Path
$vcRoot = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC'
$compiler = Get-ChildItem -LiteralPath (Join-Path $vcRoot 'Tools/MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$out = Join-Path $repo 'Build/Obj/Phase13Jobs/SubmissionLifetime'
New-Item -ItemType Directory -Force -Path $out | Out-Null

# enkiTS 1.12: widen its existing publish/read window without changing task order.
# Both sides use the same scheduler copy; installed/product enkiTS is untouched.
$upstream = [IO.File]::ReadAllText((Join-Path $sourceDir 'TaskScheduler.cpp'))
$publish = 'm_pPinnedTaskListPerThread[ pTask_->m_Priority ][ pTask_->threadNum ].WriterWriteFront( pTask_ );'
if (-not $upstream.Contains($publish)) { throw 'Expected enkiTS 1.12 publication site missing' }
$upstream = "#include <chrono>`n" + $upstream.Replace($publish,
    $publish + "`n    std::this_thread::sleep_for(std::chrono::milliseconds(1));")
$scheduler = Join-Path $out 'TaskSchedulerDelayed.cpp'
[IO.File]::WriteAllText($scheduler, $upstream)

foreach ($mutant in @($true, $false)) {
    $name = if ($mutant) {'early-delete'} else {'worker-pool'}
    $variant = Join-Path $out $name
    New-Item -ItemType Directory -Force -Path $variant | Out-Null
    $worker = Join-Path $repo 'Engine/Utility_Framework/ThreadPool.cpp'
    if ($mutant) {
        $text = [IO.File]::ReadAllText($worker)
        $retain = 'std::atomic<unsigned> references_{2};'
        $release = 'submitted->release_submission();'
        if (-not $text.Contains($retain) -or -not $text.Contains($release)) { throw 'Lifetime mutation target missing' }
        $worker = Join-Path $variant 'WorkerPoolEarlyDelete.cpp'
        [IO.File]::WriteAllText($worker, $text.Replace($retain, 'std::atomic<unsigned> references_{1};').Replace($release, ''))
    }
    $exe = Join-Path $variant 'probe.exe'
    $command = 'call "' + $vcRoot + '/Auxiliary/Build/vcvars64.bat" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /Zi /Od /MD /fsanitize=address /DWORKER_POOL_LIFETIME_STRESS ' +
        '/I"' + (Join-Path $repo 'Engine/Utility_Framework') + '" /I"' + (Join-Path $repo 'vcpkg_installed/x64-windows/include') + '" /I"' + $sourceDir + '" ' +
        '/Fo"' + $variant + '/" /Fd"' + $variant + '/probe.pdb" /Fe"' + $exe + '" "' + $worker + '" "' +
        (Join-Path $repo 'Engine/Utility_Framework/JobScheduler.cpp') + '" "' + (Join-Path $PSScriptRoot 'worker_pool_probe.cpp') + '" "' + $scheduler + '" /link /NODEFAULTLIB:enkiTS.lib'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name ASan build failed" }
    Copy-Item -LiteralPath (Join-Path $compiler.FullName 'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll') -Destination $variant
    $proc = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $variant 'run.out') -RedirectStandardError (Join-Path $variant 'run.err')
    $processHandle = $proc.Handle
    if (-not $proc.WaitForExit(90000)) { $proc.Kill(); $proc.WaitForExit(); throw "$name timed out" }
    $proc.WaitForExit()
    $stdout = Get-Content (Join-Path $variant 'run.out') -Raw
    $stderr = Get-Content (Join-Path $variant 'run.err') -Raw
    if ($mutant) {
        if ($proc.ExitCode -eq 0 -or $stderr -notmatch 'heap-use-after-free' -or $stderr -notmatch 'AddPinnedTaskInt') {
            throw "Early-delete mutant was not caught at publication (exit=$($proc.ExitCode)): $stderr"
        }
        Write-Output 'WORKER_LIFETIME_MUTATION_OK heap-use-after-free AddPinnedTaskInt'
    } else {
        if ($proc.ExitCode -ne 0 -or $stdout -notmatch 'JOB_SCHEDULER_OK' -or $stderr -match 'AddressSanitizer') {
            throw "Fixed lifetime failed (exit=$($proc.ExitCode)): $stderr"
        }
        Write-Output "WORKER_LIFETIME_OK $stdout"
    }
}
