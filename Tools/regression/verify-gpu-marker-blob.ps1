$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo 'Build/Obj/GpuMarkerBlob-Debug'
New-Item $out -ItemType Directory -Force | Out-Null
. "$PSScriptRoot/ContractProbeConfiguration.ps1"
$config = New-ContractProbeConfiguration -Configuration Debug -OutputDirectory $out
$toolchain = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231'
$sdk = 'C:/Program Files (x86)/Windows Kits/10'
$env:INCLUDE = "$toolchain/include;$sdk/Include/10.0.26100.0/ucrt;$sdk/Include/10.0.26100.0/shared;$sdk/Include/10.0.26100.0/um;$sdk/Include/10.0.26100.0/winrt"
$env:LIB = "$toolchain/lib/x64;$sdk/Lib/10.0.26100.0/ucrt/x64;$sdk/Lib/10.0.26100.0/um/x64"
$env:PATH = "$toolchain/bin/Hostx64/x64;" + $env:PATH
$compilerArgs = @('/nologo','/EHsc','/std:c++latest','/utf-8','/MDd','/Od','/RTC1','/D_DEBUG') + $config.Arguments + @(
    "/I$repo/Engine/RenderEngine", "/I$repo/Engine/Utility_Framework", "/I$repo/ThirdParty/Mathematics/include",
    "/Fo$out/", "/Fe$out/probe.exe", "$PSScriptRoot/gpu_marker_blob_probe.cpp", "$repo/Engine/EngineDiagnostics/ProfileMarker.cpp")
& "$toolchain/bin/Hostx64/x64/cl.exe" @compilerArgs
if ($LASTEXITCODE)
{
    throw 'Marker/blob probe compilation failed'
}
$p = Start-Process "$out/probe.exe" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log"
if (!$p.WaitForExit(60000))
{
    Stop-Process -Id $p.Id -Force
    throw 'Marker/blob probe timed out'
}
Get-Content "$out/stdout.log"
Get-Content "$out/stderr.log"
if ($p.ExitCode -ne 0)
{
    throw "Marker/blob probe failed $($p.ExitCode)"
}
