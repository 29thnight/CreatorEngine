#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(4, 256)][int]$Frames = 24,
    [ValidateRange(64, 4096)][int]$Draws = 1024,
    [ValidateRange(2, 1024)][int]$DistinctMeshes = 64,
    [ValidateSet('same', 'distinct')][string[]]$Cases = @('same', 'distinct')
)
$ErrorActionPreference = 'Stop'
if (!('GeometryThreadNames' -as [type]))
{
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class GeometryThreadNames
{
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr text);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr value);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr value);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    public static string Name(uint id)
    {
        IntPtr thread = OpenThread(0x800, false, id);
        if (thread == IntPtr.Zero) return "";
        try
        {
            IntPtr text;
            if (GetThreadDescription(thread, out text) < 0) return "";
            try { return Marshal.PtrToStringUni(text) ?? ""; }
            finally { LocalFree(text); }
        }
        finally { CloseHandle(thread); }
    }
    public static uint ForegroundPid()
    {
        uint pid;
        GetWindowThreadProcessId(GetForegroundWindow(), out pid);
        return pid;
    }
}
"@
}

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
if ($DistinctMeshes -gt $Draws)
{
    throw 'Distinct mesh count exceeds the draw count.'
}
function Request-Etw([string]$Action, [string]$Name)
{
    $id = [guid]::NewGuid().ToString('N')
    $temporary = Join-Path $out 'etw-request.tmp'
    @{id = $id; action = $Action; name = $Name} | ConvertTo-Json | Set-Content $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination (Join-Path $out 'etw-request.json') -Force
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do
    {
        Start-Sleep -Milliseconds 100
        $reply = $null
        try
        {
            $reply = Get-Content (Join-Path $out 'etw-reply.json') -Raw | ConvertFrom-Json
        }
        catch [System.Management.Automation.ItemNotFoundException]
        {
            # The collector replaces a completed reply; retain the same request id.
        }
        catch [System.IO.IOException]
        {
        }
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw "ETW $Action acknowledgement timeout."
        }
    } while ($reply.id -ne $id)
    if ($reply.exitCode -ne 0)
    {
        throw "ETW $Action failed: $($reply | ConvertTo-Json -Compress)"
    }
}
$results = @()
foreach ($case in $Cases)
{
    foreach ($recording in @('on', 'off'))
    {
        foreach ($variant in @('before-a', 'after-a', 'after-b', 'before-b'))
        {
            $build, $repeat = $variant.Split('-')
            $name = "$build-$case-$repeat-$recording"
            $directory = Join-Path $out $name
            if (Test-Path -LiteralPath $directory)
            {
                throw "Fresh output directory required: $directory"
            }
            $null = New-Item $directory -ItemType Directory
            $executable = Join-Path $out "binaries/$build/MaterialRasterSurfaceProbe.exe"
            $start = [Diagnostics.ProcessStartInfo]::new($executable)
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.WindowStyle = 'Hidden'
            $start.WorkingDirectory = $repo
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            $start.ArgumentList.Add($repo)
            $start.ArgumentList.Add('--geometry-preparation-scale-only')
            $start.Environment['PATH'] = "$repo/vcpkg_installed/x64-windows/debug/bin;$repo/vcpkg_installed/x64-windows/x64-windows/debug/bin;" + $env:PATH
            $start.Environment['CREATOR_DX12_VALIDATION'] = 'off'
            $start.Environment['CREATOR_GEOMETRY_SCALE_DRAWS'] = "$Draws"
            $start.Environment['CREATOR_GEOMETRY_SCALE_UNIQUE'] = $(if ($case -eq 'same') {'1'} else {"$DistinctMeshes"})
            $start.Environment['CREATOR_GEOMETRY_SCALE_FRAMES'] = "$Frames"
            $start.Environment['CREATOR_GEOMETRY_SCALE_RECORD'] = $(if ($recording -eq 'on') {'1'} else {'0'})
            $start.Environment['CREATOR_GEOMETRY_SCALE_CAPTURE'] = Join-Path $directory 'native.ceprof'
            $start.Environment['CREATOR_GEOMETRY_SCALE_GATE'] = Join-Path $directory 'gate'
            $process = [Diagnostics.Process]::Start($start)
            $stdout = [IO.File]::Create((Join-Path $directory 'stdout.log'))
            $stderr = [IO.File]::Create((Join-Path $directory 'stderr.log'))
            $copyOut = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
            $copyError = $process.StandardError.BaseStream.CopyToAsync($stderr)
            $traceOwned = $false
            try
            {
                $deadline = [DateTime]::UtcNow.AddSeconds(600)
                while (!(Test-Path -LiteralPath (Join-Path $directory 'gate.ready')))
                {
                    if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline)
                    {
                        throw "Fixture did not warm: $name"
                    }
                    Start-Sleep -Milliseconds 100
                }
                Request-Etw 'start' $name
                $traceOwned = $true
                $started = @{processId = $process.Id; qpc = [Diagnostics.Stopwatch]::GetTimestamp();
                    hz = [Diagnostics.Stopwatch]::Frequency; utc = [DateTime]::UtcNow.ToString('o');
                    threadIds = @($process.Threads | ForEach-Object { $_.Id });
                    executableSha256 = (Get-FileHash $executable).Hash; draws = $Draws;
                    uniqueMeshes = $start.Environment['CREATOR_GEOMETRY_SCALE_UNIQUE']; recording = $recording}
                $started.threadSnapshots = @($process.Threads | ForEach-Object {
                    @{tid=$_.Id;name=[GeometryThreadNames]::Name($_.Id);basePriority=$_.BasePriority;currentPriority=$_.CurrentPriority}
                })
                $started.foregroundPid = [GeometryThreadNames]::ForegroundPid()
                $started.processPriorityClass = $process.PriorityClass.ToString()
                $started.affinityMask = $process.ProcessorAffinity.ToInt64().ToString()
                $started.logicalProcessors = [Environment]::ProcessorCount
                $started | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $directory 'started.json') -Encoding utf8
                Set-Content (Join-Path $directory 'gate.go') 'collect' -Encoding utf8
                if (!$process.WaitForExit(600000))
                {
                    throw "Measurement timeout: $name"
                }
            }
            finally
            {
                if (!$process.HasExited)
                {
                    $process.Kill()
                    $process.WaitForExit()
                }
                if ($traceOwned)
                {
                    Request-Etw 'stop' $name
                }
                $null = $copyOut.GetAwaiter().GetResult()
                $null = $copyError.GetAwaiter().GetResult()
                $stdout.Dispose()
                $stderr.Dispose()
            }
            $log = Get-Content (Join-Path $directory 'stdout.log') -Raw
            $errors = Get-Content (Join-Path $directory 'stderr.log') -Raw
            $samples = @(Get-Content (Join-Path $directory 'stdout.log') | Where-Object { $_.StartsWith('GEOMETRY_SCALE_FRAME ') } |
                ForEach-Object { $_.Substring(21) | ConvertFrom-Json })
            $passed = $process.ExitCode -eq 0 -and $log -match 'GEOMETRY_SCALE_OK' -and $samples.Count -eq $Frames -and
                ($log + $errors) -notmatch 'PROBE_CHECK_FAIL|RASTER_SURFACE_FAIL|shutdown abandoned=[1-9]'
            if ($recording -eq 'on')
            {
                $passed = $passed -and $log -match 'GEOMETRY_SCALE_CAPTURE complete=1 unacked=0 dropped=0 late=0 lateSpans=0 unbalanced=0 open=0'
            }
            $samples | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $directory 'frames.json') -Encoding utf8
            $result = @{name = $name; passed = [bool]$passed; exitCode = $process.ExitCode; samples = $samples.Count;
                configuration = 'Debug'; nativeProductSceneHost = $true; visibleInstances = 1;
                perFrameGpuReadbackWait = $true; displayFpsMeasurement = $false; provenance = $started}
            $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $directory 'result.json') -Encoding utf8
            $results += $result
            $results | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'runs.json') -Encoding utf8
            Write-Output "GEOMETRY_MEASURED $name passed=$passed frames=$($samples.Count)"
            if (!$passed)
            {
                throw "Geometry preparation measurement failed: $name"
            }
        }
    }
}
