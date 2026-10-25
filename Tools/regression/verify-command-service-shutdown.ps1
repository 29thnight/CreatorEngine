[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Canary,
    [string]$BaselineRevision = '0847ceeb2ec99d1b4edbdfd9f7f8a94bd773f915'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $repo ('Build/Verification/CommandServiceShutdown/Contracts-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC toolchain unavailable' }

function Build-Probe([string]$SourceRoot, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $sources = @(Join-Path $SourceRoot 'Tools/regression/CommandServiceShutdownProbe.cpp')
    $sources += @(Get-ChildItem (Join-Path $SourceRoot 'Engine/CommandService') -Filter '*.cpp' -File | ForEach-Object FullName)
    $quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $flags = if ($Configuration -eq 'Debug') { '/MDd /Od /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $batch = Join-Path $Destination 'build.cmd'
    @"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /utf-8 /DCE_CONTRACT_STANDALONE=1 /W4 $flags $quotedSources /Fo"$Destination\\" /Fd"$Destination\probe.pdb" /Fe"$Destination\probe.exe" /link Ws2_32.lib Bcrypt.lib Advapi32.lib
"@ | Set-Content -LiteralPath $batch -Encoding ascii
    & $env:ComSpec /d /c $batch *> (Join-Path $Destination 'build.log')
    if ($LASTEXITCODE) { throw "Service shutdown probe build failed: $Destination/build.log" }
}

function Run-Probe([string]$Directory, [switch]$KeepAliveOnly) {
    $arguments = @(('"' + $Directory + '/work"'))
    if ($KeepAliveOnly) { $arguments += '--keepalive-only' }
    $process = Start-Process -FilePath "$Directory/probe.exe" -ArgumentList $arguments -WindowStyle Hidden `
        -RedirectStandardOutput "$Directory/stdout.log" -RedirectStandardError "$Directory/stderr.log" -PassThru
    $null = $process.Handle
    try {
        if (!$process.WaitForExit(20000)) { throw "Service shutdown probe timed out: $Directory" }
        $process.WaitForExit()
        $text = [IO.File]::ReadAllText("$Directory/stdout.log") + [IO.File]::ReadAllText("$Directory/stderr.log")
        [IO.File]::WriteAllText("$Directory/run.log", $text, [Text.UTF8Encoding]::new($false))
        return $process.ExitCode
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

Push-Location $repo
try {
    $current = Join-Path $output 'current'
    Build-Probe $repo $current
    $currentExit = Run-Probe $current
    if ($currentExit -ne 0) { throw "Service shutdown regression failed: $current/run.log" }
    $text = [IO.File]::ReadAllText("$current/run.log")
    $caseMatches = [regex]::Matches($text, 'SERVICE_SHUTDOWN_CASE name=([^ ]+) elapsedMs=([0-9.]+) timeoutMs=5000')
    if ($caseMatches.Count -ne 17 -or $text -notmatch 'COMMAND_SERVICE_SHUTDOWN_OK') { throw 'Incomplete shutdown cases' }

    $canaryResult = $null
    if ($Canary) {
        # Keep the working tree intact. Compile the pre-fix module in an isolated
        # source mirror, including its matching header and the same test input.
        $baseline = Join-Path $output 'baseline'
        New-Item -ItemType Directory -Force -Path "$baseline/Engine/CommandService", "$baseline/Engine/Utility_Framework", "$baseline/Tools/regression" | Out-Null
        Get-ChildItem "$repo/Engine/CommandService" -File | Copy-Item -Destination "$baseline/Engine/CommandService"
        Copy-Item "$repo/Engine/Utility_Framework/Foundation/StandardLibrary.h" "$baseline/Engine/Utility_Framework"
        Copy-Item "$repo/Tools/regression/CommandServiceShutdownProbe.cpp" "$baseline/Tools/regression"
        foreach ($name in @('CommandService.cpp', 'CommandService.h')) {
            $original = & git show "${BaselineRevision}:Engine/CommandService/$name"
            if ($LASTEXITCODE) { throw 'Cannot read the pre-fix module baseline' }
            [IO.File]::WriteAllText("$baseline/Engine/CommandService/$name", ($original -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
        }
        $baselineBinary = Join-Path $baseline 'binary'
        Build-Probe $baseline $baselineBinary
        $baselineExit = Run-Probe $baselineBinary -KeepAliveOnly
        $baselineText = [IO.File]::ReadAllText("$baselineBinary/run.log")
        if ($baselineExit -eq 0 -or $baselineText -notmatch 'COMMAND_SERVICE_SHUTDOWN_FAILED: Stop must interrupt workers') {
            throw 'Pre-fix canary did not reproduce the shutdown deadline failure'
        }
        $canaryResult = @{ result = 'PRE_FIX_KEEPALIVE_SHUTDOWN_REJECTED'; revision = $BaselineRevision; exitCode = $baselineExit; log = "$baselineBinary/run.log" }
    }

    $cases = @($caseMatches | ForEach-Object {
        @{ name = $_.Groups[1].Value; elapsedMs = [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) }
    })
    $hashes = @{}
    foreach ($path in @('Engine/CommandService/CommandService.cpp', 'Engine/CommandService/CommandService.h', 'Engine/CommandService/SocketPlatform.h', 'Engine/CommandService/SocketPlatform_Win32.cpp', 'Tools/regression/CommandServiceShutdownProbe.cpp')) {
        $hashes[$path] = (Get-FileHash -LiteralPath (Join-Path $repo $path)).Hash
    }
    @{ result = 'COMMAND_SERVICE_SHUTDOWN_OK'; configuration = $Configuration; cases = $cases; canary = $canaryResult; probe = "$current/probe.exe";
       sourceHashes = $hashes; scope = 'Real Winsock/HTTP CommandService with a controlled gateway; Editor/Player acceptance is separate' } |
        ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$output/result.json" -Encoding utf8
    "COMMAND_SERVICE_SHUTDOWN_OK configuration=$Configuration cases=17 evidence=$output"
} finally {
    Pop-Location
}
