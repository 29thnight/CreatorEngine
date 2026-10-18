$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'PerformanceEnvironment.ps1')
$checks = 0
function Assert-EnvironmentTest
{
    param([bool]$Condition, [string]$Message)
    if (-not $Condition)
    {
        throw $Message
    }
    ++$script:checks
}
function New-TestProcess
{
    param([int]$ProcessId, [double]$Cpu, [long]$Qpc)
    [pscustomobject]@{
        processId = $ProcessId
        name = "process-$ProcessId"
        startTimeUtc = '2026-10-18T00:00:00.0000000Z'
        cpuSeconds = $Cpu
        cpuSampleQpc = $Qpc
        priorityClass = 'Normal'
        basePriority = 8
    }
}
function New-TestSnapshot
{
    param([long]$Qpc, [object[]]$Processes)
    [pscustomobject]@{
        qpcBegin = $Qpc
        qpcEnd = $Qpc + 100
        qpcFrequency = 1000
        logicalProcessorCount = 8
        foregroundProcessIdBegin = 20
        foregroundProcessIdEnd = 20
        processes = $Processes
    }
}
function Invoke-TestComparison
{
    param($Before, $After)
    Compare-PerformanceEnvironment -Before $Before -After $After -TargetProcessId 10 `
        -MeasurementBeginQpc 11000 -MeasurementEndQpc 19000
}
$before = New-TestSnapshot 9900 @((New-TestProcess 10 3 10000), (New-TestProcess 20 1 10000))
$after = New-TestSnapshot 20000 @((New-TestProcess 10 5 20000), (New-TestProcess 20 5 20000))
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.measurementSeconds -eq 8) 'Actual measurement window must remain eight seconds'
Assert-EnvironmentTest ($result.knownExternalMachineCpuPercent -eq 5) 'External CPU must use ten observed seconds and eight CPUs'
Assert-EnvironmentTest ($result.externalMachineCpuPercent -eq 5) 'Complete observation should publish external CPU'
Assert-EnvironmentTest ($result.processes[0].machineCpuPercent -eq 2.5) 'Target CPU normalization is incorrect'
Assert-EnvironmentTest ($result.beforeCollectionMs -eq 100 -and $result.afterCollectionMs -eq 100) 'Collection duration missing'
Assert-EnvironmentTest ($result.beforeCollectionToWindowMs -eq 1000 -and $result.afterWindowToCollectionMs -eq 1000) 'Boundary gaps missing'
Assert-EnvironmentTest ($result.processes[1].beforeBoundaryGapMs -eq 1000 -and $result.processes[1].afterBoundaryGapMs -eq 1000) 'Per-process gaps missing'
$after.processes[1].priorityClass = $null
$after.processes[1].basePriority = $null
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.externalMachineCpuPercent -eq 5 -and $null -eq $result.processes[1].basePriorityAfter) 'Denied priority access must not fabricate a priority or discard a valid CPU delta'
$after.processes[1].priorityClass = 'AboveNormal'
$after.processes[1].basePriority = 10
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].basePriorityBefore -eq 8 -and $result.processes[1].basePriorityAfter -eq 10) 'Priority changes must remain visible'

$after.processes[1].startTimeUtc = '2026-10-18T00:00:01.0000000Z'
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-pid-reused' -and $null -eq $result.processes[1].cpuSeconds) 'PID reuse must not produce a CPU delta'
Assert-EnvironmentTest ($null -eq $result.externalMachineCpuPercent -and $result.unknownProcessCount -eq 1) 'Unknown process must prevent an apparent complete total'
$after.processes[1].startTimeUtc = $before.processes[1].startTimeUtc
$after.processes[1].cpuSeconds = $null
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-cpu-time' -and $null -eq $result.processes[1].machineCpuPercent) 'Denied CPU access must stay unknown'
$after.processes[1].cpuSeconds = 5
$after.processes[1].startTimeUtc = $null
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-identity') 'Denied creation-time access must stay unknown'
$after.processes = @($after.processes[0], (New-TestProcess 30 6 20000))
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-exited-or-unavailable') 'An exited process must stay unknown'
Assert-EnvironmentTest ($result.processes[2].status -eq 'unknown-new-process') 'A new process must stay unknown'
Assert-EnvironmentTest ($result.unknownProcessCount -eq 2) 'Missing processes must both be counted'

$before = New-TestSnapshot 9900 @((New-TestProcess 0 200 10000), (New-TestProcess 10 3 10000))
$after = New-TestSnapshot 20000 @((New-TestProcess 0 300 20000), (New-TestProcess 10 5 20000))
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[0].status -eq 'idle-process-excluded' -and $result.externalMachineCpuPercent -eq 0) 'Idle process is not background CPU competition'
$after.processes[1].cpuSeconds = 2
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-invalid-counter-or-time') 'Regressing CPU counters must stay unknown'
$after.processes[1].cpuSeconds = 5
$after.processes[1].cpuSampleQpc = 19999
$result = Invoke-TestComparison $before $after
Assert-EnvironmentTest ($result.processes[1].status -eq 'unknown-invalid-counter-or-time') 'CPU sample outside snapshot bounds must stay unknown'

foreach ($invalid in @('frequency', 'processors', 'overlap', 'reversed'))
{
    $candidate = New-TestSnapshot 20000 @((New-TestProcess 10 5 20000))
    switch ($invalid)
    {
        'frequency'
        {
            $candidate.qpcFrequency = 2000
        }
        'processors'
        {
            $candidate.logicalProcessorCount = 4
        }
        'overlap'
        {
            $candidate.qpcBegin = 18500
        }
        'reversed'
        {
            $candidate.qpcEnd = 19900
        }
    }
    $rejected = $false
    try
    {
        Invoke-TestComparison $before $candidate | Out-Null
    }
    catch
    {
        $rejected = $true
    }
    Assert-EnvironmentTest $rejected "Invalid $invalid boundaries must be rejected"
}
Write-Output "PERFORMANCE_ENVIRONMENT_OK checks=$checks"
