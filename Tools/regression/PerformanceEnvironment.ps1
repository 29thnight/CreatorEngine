# 구간 경계의 관측은 배경 CPU 경쟁 자료이며 유휴 상태나 Ready 지연의 원인 판정이 아니다.
# 실제 실행 경쟁은 ETW 문맥 전환으로 확인한다. WMI 조회와 우선순위 변경은 하지 않는다.
function Initialize-PerformanceEnvironment
{
    if (-not ('Creator.PerformanceEnvironment.NativeMethods' -as [type]))
    {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Creator.PerformanceEnvironment
{
    public static class NativeMethods
    {
        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("kernel32.dll")]
        public static extern uint GetActiveProcessorCount(ushort groupNumber);

        public static uint ForegroundProcessId()
        {
            uint processId;
            GetWindowThreadProcessId(GetForegroundWindow(), out processId);
            return processId;
        }
    }
}
'@
    }
}

function Get-PerformanceEnvironmentSnapshot
{
    Initialize-PerformanceEnvironment
    $beginQpc = [Diagnostics.Stopwatch]::GetTimestamp()
    $foregroundBegin = [Creator.PerformanceEnvironment.NativeMethods]::ForegroundProcessId()
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($process in [Diagnostics.Process]::GetProcesses())
    {
        $row = [ordered]@{
            processId = $process.Id
            name = $null
            startTimeUtc = $null
            cpuSeconds = $null
            cpuSampleQpc = $null
            priorityClass = $null
            basePriority = $null
            errors = [Collections.Generic.List[string]]::new()
        }
        try
        {
            try
            {
                $row.name = $process.ProcessName
            }
            catch
            {
                $row.errors.Add('name-unavailable')
            }
            try
            {
                $row.startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
            }
            catch
            {
                $row.errors.Add('start-time-unavailable')
            }
            try
            {
                $cpuBegin = [Diagnostics.Stopwatch]::GetTimestamp()
                $row.cpuSeconds = $process.TotalProcessorTime.TotalSeconds
                $cpuEnd = [Diagnostics.Stopwatch]::GetTimestamp()
                $row.cpuSampleQpc = $cpuBegin + [long](($cpuEnd - $cpuBegin) / 2)
            }
            catch
            {
                $row.errors.Add('cpu-time-unavailable')
            }
            try
            {
                $row.priorityClass = $process.PriorityClass.ToString()
                $row.basePriority = $process.BasePriority
            }
            catch
            {
                $row.errors.Add('priority-unavailable')
            }
            $row.errors = $row.errors.ToArray()
            $rows.Add([pscustomobject]$row)
        }
        finally
        {
            $process.Dispose()
        }
    }
    $foregroundEnd = [Creator.PerformanceEnvironment.NativeMethods]::ForegroundProcessId()
    $endQpc = [Diagnostics.Stopwatch]::GetTimestamp()
    [pscustomobject]@{
        schemaVersion = 1
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        qpcBegin = $beginQpc
        qpcEnd = $endQpc
        qpcFrequency = [Diagnostics.Stopwatch]::Frequency
        logicalProcessorCount = [Creator.PerformanceEnvironment.NativeMethods]::GetActiveProcessorCount(0xFFFF)
        foregroundProcessIdBegin = $foregroundBegin
        foregroundProcessIdEnd = $foregroundEnd
        processes = $rows.ToArray()
    }
}

function Compare-PerformanceEnvironment
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Before,
        [Parameter(Mandatory)]$After,
        [Parameter(Mandatory)][int]$TargetProcessId,
        [Parameter(Mandatory)][long]$MeasurementBeginQpc,
        [Parameter(Mandatory)][long]$MeasurementEndQpc
    )
    $frequency = [double]$Before.qpcFrequency
    $logicalProcessors = [int]$Before.logicalProcessorCount
    if ($frequency -le 0 -or $frequency -ne [double]$After.qpcFrequency -or
        $logicalProcessors -le 0 -or $logicalProcessors -ne [int]$After.logicalProcessorCount)
    {
        throw 'Environment snapshots must use the same positive QPC frequency and logical processor count'
    }
    if ($Before.qpcBegin -gt $Before.qpcEnd -or $After.qpcBegin -gt $After.qpcEnd -or
        $Before.qpcEnd -gt $MeasurementBeginQpc -or $MeasurementBeginQpc -ge $MeasurementEndQpc -or
        $MeasurementEndQpc -gt $After.qpcBegin)
    {
        throw 'Environment collection must bracket the measured window without overlapping it'
    }
    $beforeById = @{}
    $afterById = @{}
    $processIds = [Collections.Generic.HashSet[int]]::new()
    foreach ($entry in $Before.processes)
    {
        if ($beforeById.ContainsKey([int]$entry.processId))
        {
            throw 'Duplicate process ID in the before snapshot'
        }
        $beforeById[[int]$entry.processId] = $entry
        [void]$processIds.Add([int]$entry.processId)
    }
    foreach ($entry in $After.processes)
    {
        if ($afterById.ContainsKey([int]$entry.processId))
        {
            throw 'Duplicate process ID in the after snapshot'
        }
        $afterById[[int]$entry.processId] = $entry
        [void]$processIds.Add([int]$entry.processId)
    }
    $rows = [Collections.Generic.List[object]]::new()
    $knownExternalCpuSeconds = 0.0
    $knownExternalCpuPercent = 0.0
    $unknownProcessCount = 0
    foreach ($processId in ($processIds | Sort-Object))
    {
        $begin = $beforeById[$processId]
        $end = $afterById[$processId]
        $row = [ordered]@{
            processId = $processId
            name = $null
            target = $processId -eq $TargetProcessId
            status = 'unknown'
            cpuSeconds = $null
            observationSeconds = $null
            machineCpuPercent = $null
            beforeBoundaryGapMs = $null
            afterBoundaryGapMs = $null
            priorityClassBefore = $null
            priorityClassAfter = $null
            basePriorityBefore = $null
            basePriorityAfter = $null
        }
        if ($null -ne $begin)
        {
            $row.name = $begin.name
            $row.priorityClassBefore = $begin.priorityClass
            $row.basePriorityBefore = $begin.basePriority
        }
        if ($null -ne $end)
        {
            $row.name = $end.name
            $row.priorityClassAfter = $end.priorityClass
            $row.basePriorityAfter = $end.basePriority
        }
        if ($processId -eq 0)
        {
            $row.status = 'idle-process-excluded'
        }
        elseif ($null -eq $begin)
        {
            $row.status = 'unknown-new-process'
        }
        elseif ($null -eq $end)
        {
            $row.status = 'unknown-exited-or-unavailable'
        }
        elseif ([string]::IsNullOrEmpty($begin.startTimeUtc) -or [string]::IsNullOrEmpty($end.startTimeUtc))
        {
            $row.status = 'unknown-identity'
        }
        elseif ($begin.startTimeUtc -ne $end.startTimeUtc)
        {
            $row.status = 'unknown-pid-reused'
        }
        elseif ($null -eq $begin.cpuSeconds -or $null -eq $end.cpuSeconds -or
            $null -eq $begin.cpuSampleQpc -or $null -eq $end.cpuSampleQpc)
        {
            $row.status = 'unknown-cpu-time'
        }
        elseif ($begin.cpuSampleQpc -lt $Before.qpcBegin -or $begin.cpuSampleQpc -gt $Before.qpcEnd -or
            $end.cpuSampleQpc -lt $After.qpcBegin -or $end.cpuSampleQpc -gt $After.qpcEnd -or
            $end.cpuSampleQpc -le $begin.cpuSampleQpc -or $end.cpuSeconds -lt $begin.cpuSeconds)
        {
            $row.status = 'unknown-invalid-counter-or-time'
        }
        else
        {
            $row.status = 'observed'
            $row.cpuSeconds = [double]$end.cpuSeconds - [double]$begin.cpuSeconds
            $row.observationSeconds = ([double]$end.cpuSampleQpc - [double]$begin.cpuSampleQpc) / $frequency
            $row.machineCpuPercent = 100.0 * $row.cpuSeconds / $row.observationSeconds / $logicalProcessors
            $row.beforeBoundaryGapMs = 1000.0 * ($MeasurementBeginQpc - $begin.cpuSampleQpc) / $frequency
            $row.afterBoundaryGapMs = 1000.0 * ($end.cpuSampleQpc - $MeasurementEndQpc) / $frequency
            if (-not $row.target)
            {
                $knownExternalCpuSeconds += $row.cpuSeconds
                $knownExternalCpuPercent += $row.machineCpuPercent
            }
        }
        if ($row.status.StartsWith('unknown-'))
        {
            ++$unknownProcessCount
        }
        $rows.Add([pscustomobject]$row)
    }
    $externalCpuPercent = $null
    if ($unknownProcessCount -eq 0)
    {
        $externalCpuPercent = $knownExternalCpuPercent
    }
    [pscustomobject]@{
        schemaVersion = 1
        scope = 'background-evidence-from-boundaries-not-quiet-or-causal-acceptance'
        targetProcessId = $TargetProcessId
        logicalProcessorCount = $logicalProcessors
        measurementBeginQpc = $MeasurementBeginQpc
        measurementEndQpc = $MeasurementEndQpc
        measurementSeconds = ($MeasurementEndQpc - $MeasurementBeginQpc) / $frequency
        beforeCollectionMs = 1000.0 * ($Before.qpcEnd - $Before.qpcBegin) / $frequency
        afterCollectionMs = 1000.0 * ($After.qpcEnd - $After.qpcBegin) / $frequency
        beforeCollectionToWindowMs = 1000.0 * ($MeasurementBeginQpc - $Before.qpcEnd) / $frequency
        afterWindowToCollectionMs = 1000.0 * ($After.qpcBegin - $MeasurementEndQpc) / $frequency
        knownExternalCpuSeconds = $knownExternalCpuSeconds
        knownExternalMachineCpuPercent = $knownExternalCpuPercent
        externalMachineCpuPercent = $externalCpuPercent
        unknownProcessCount = $unknownProcessCount
        normalization = 'Sum of observed process CPU seconds / its CPU sample interval / logical processors * 100'
        limitation = 'Intervals include reported boundary gaps; processes born and exited between snapshots are unobserved. ETW is required for exact-window attribution.'
        foregroundProcessIds = @($Before.foregroundProcessIdBegin, $Before.foregroundProcessIdEnd,
            $After.foregroundProcessIdBegin, $After.foregroundProcessIdEnd)
        processes = $rows.ToArray()
    }
}
