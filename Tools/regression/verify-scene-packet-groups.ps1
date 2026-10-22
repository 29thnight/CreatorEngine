param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo 'Build/Obj/ScenePacketGroups-Debug'
New-Item $out -ItemType Directory -Force | Out-Null
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX /MDd /Od /RTC1 /D_DEBUG ' +
    '/Fo"' + $out + '/" /Fe"' + $out + '/probe.exe" "' + $PSScriptRoot + '/scene_packet_groups_probe.cpp"'
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE)
{
    throw 'Scene packet group probe compilation failed.'
}
$process = Start-Process -FilePath "$out/probe.exe" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log"
if (-not $process.WaitForExit(30000))
{
    Stop-Process -Id $process.Id -Force
    throw 'Scene packet group regression timed out after 30 seconds.'
}
Get-Content "$out/stdout.log"
Get-Content "$out/stderr.log"
if ($process.ExitCode -ne 0)
{
    throw "Scene packet group regression failed: $($process.ExitCode)."
}
