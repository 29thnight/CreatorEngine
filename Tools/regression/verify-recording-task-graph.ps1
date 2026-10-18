param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
# DAG 순서·수명·완료 대기와 종료를 검증한다. 실제 지연 시간·처리율을 판정하는 벤치마크가 아니다.
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/Resolve-EnkiDependencies.ps1"
$dependencies = Resolve-EnkiDependencies $repo
$out = "$repo/Build/Obj/RecordingTaskGraph-$Configuration"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$lib = if ($Configuration -eq 'Debug') { Join-Path $dependencies 'debug' } else { $dependencies }
Copy-Item -LiteralPath "$lib/bin/enkiTS.dll" -Destination $out
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$flags = if ($Configuration -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
foreach ($mutant in @($false, $true))
{
    $name = if ($mutant) { 'wave-join-mutant' } else { 'recording-task-graph' }
    $extra = if ($mutant) { '/DRECORDING_WAVE_JOIN_MUTANT' } else { '' }
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        '/DENKITS_DLL=1 /DCE_CONTRACT_STANDALONE ' + $flags + ' ' + $extra +
        ' /I"' + "$repo/Engine/Utility_Framework" + '" /I"' + "$repo/Engine/RenderEngine" +
        '" /external:I"' + "$dependencies/include" + '" /external:W0 /Fo"' + $out +
        '/" /Fd"' + "$out/$name.pdb" + '" /Fe"' + "$out/$name.exe" + '" "' +
        "$repo/Engine/Utility_Framework/JobScheduler.cpp" + '" "' +
        "$repo/Engine/Utility_Framework/ThreadPool.cpp" + '" "' +
        "$PSScriptRoot/recording_task_graph_probe.cpp" + '" /link /LIBPATH:"' + "$lib/lib" + '"'
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE) { throw 'Recording task graph probe compilation failed.' }
    $process = Start-Process -FilePath "$out/$name.exe" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/$name.out" -RedirectStandardError "$out/$name.err"
    if (!$process.WaitForExit(30000))
    {
        Stop-Process -Id $process.Id
        throw 'Recording task graph probe timed out.'
    }
    $process.WaitForExit()
    $stdout = [IO.File]::ReadAllText("$out/$name.out")
    $stderr = [IO.File]::ReadAllText("$out/$name.err")
    if ($mutant)
    {
        if ($process.ExitCode -ne 1 -or $stderr -notmatch 'unrelated wave work became a dependency|consumer waited for unrelated')
        {
            throw "Wave-join mutation was not detected: $stderr"
        }
        'RECORDING_TASK_GRAPH_MUTATION_OK'
    }
    else
    {
        $expected = 'RECORDING_TASK_GRAPH_OK checks=\d+ workers=1,4 policies=legacy,native ' +
            'wait=cv lifetime=callback,observer shutdown=drain topology=bounded'
        if ($process.ExitCode -ne 0 -or $stdout -notmatch $expected)
        {
            throw "Recording task graph regression failed: $stderr"
        }
        $stdout
    }
}
