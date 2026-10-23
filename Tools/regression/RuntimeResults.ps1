# Result readers deliberately reject incomplete observations. Human logs remain
# available for diagnosis but never supply a missing structured result.
Set-StrictMode -Version Latest

function Assert-ResultInteger($Value, [string]$Field, [long]$Minimum = 0) {
    if (($Value -isnot [int] -and $Value -isnot [long]) -or $Value -lt $Minimum) {
        throw "Invalid integer result field: $Field"
    }
}

function Assert-ResultBoolean($Value, [string]$Field) {
    if ($Value -isnot [bool]) { throw "Invalid boolean result field: $Field" }
}

function Read-RuntimeResults([string]$Path, [int]$ProcessId) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Runtime result file missing: $Path"
    }

    $rows = @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
    if ($rows.Count -eq 0) { throw "Empty runtime result file: $Path" }

    $sequence = 0
    foreach ($row in $rows) {
        $sequence++
        Assert-ResultInteger $row.schemaVersion 'schemaVersion' 1
        Assert-ResultInteger $row.processId 'processId' 1
        Assert-ResultInteger $row.sequence 'sequence' 1
        if ($row.schemaVersion -ne 1 -or $row.processId -ne $ProcessId -or
            $row.sequence -ne $sequence -or [string]::IsNullOrWhiteSpace($row.event) -or
            $null -eq $row.data) {
            throw "Invalid runtime result identity/order in $Path at row $sequence"
        }
    }
    if ($rows[0].event -ne 'session.started') { throw 'Runtime session boundary missing' }
    return $rows
}

function Get-RuntimeResult($Results, [string]$Event) {
    $rows = @($Results | Where-Object event -CEQ $Event)
    if ($rows.Count -ne 1) { throw "Expected one $Event event, found $($rows.Count)" }
    return $rows[0]
}

function Read-TaggedResult([string]$Text, [string]$Tag, [int]$ProcessId) {
    $rows = @($Text -split "`n" | Where-Object { $_.StartsWith($Tag + ' ') })
    if ($rows.Count -ne 1) { throw "Expected one $Tag result, found $($rows.Count)" }
    $row = $rows[0].Substring($Tag.Length + 1) | ConvertFrom-Json -ErrorAction Stop
    Assert-ResultInteger $row.schemaVersion 'schemaVersion' 1
    Assert-ResultInteger $row.processId 'processId' 1
    if ($row.schemaVersion -ne 1 -or $row.processId -ne $ProcessId) {
        throw "Invalid $Tag result identity"
    }
    return $row
}

function Assert-RuntimeFinalized($Results) {
    $jobs = Get-RuntimeResult $Results 'jobs.shutdown'
    $runtime = Get-RuntimeResult $Results 'runtime.finalized'
    if ($jobs.sequence -ge $runtime.sequence) { throw 'Runtime finalized before scheduler shutdown' }
}

function Assert-PlayerRuntime($Results) {
    Assert-RuntimeFinalized $Results
    Assert-BackendQueuesEmpty $Results
    $parser = Get-RuntimeResult $Results 'player.text_parser'
    Assert-ResultInteger $parser.data.calls 'calls'
    if ($parser.data.calls -ne 0) { throw 'Cooked Player used an authoring text parser' }
}

function Read-PlayerSmoke([string]$Text, [int]$ProcessId, [int]$Frames, [int]$Promotions) {
    $result = Read-TaggedResult $Text '[player.smoke]' $ProcessId
    foreach ($field in @('frames', 'displayPromotions', 'submittedGameFrames', 'modelGenerationFailures', 'meshResolveFailures')) {
        Assert-ResultInteger $result.$field $field
    }
    foreach ($field in @('ready', 'scriptsLoaded')) { Assert-ResultBoolean $result.$field $field }
    if ($result.ready -cne $true -or $result.scriptsLoaded -cne $true -or
        $result.frames -lt $Frames -or $result.displayPromotions -lt $Promotions -or
        $result.submittedGameFrames -le 0 -or $result.modelGenerationFailures -ne 0 -or
        $result.meshResolveFailures -ne 0) {
        throw 'Player smoke result failed its readiness/frame/model contract'
    }
    return $result
}

function Read-AudioAcceptance([string]$Text, [int]$ProcessId) {
    $result = Read-TaggedResult $Text '[audio.acceptance]' $ProcessId
    foreach ($field in @('cycles', 'completions', 'resident', 'stream', 'mp3', 'flac')) {
        Assert-ResultInteger $result.$field $field
    }
    foreach ($field in @('gcValueHandle', 'worldScope', 'sessionScope', 'attached', 'positioned')) {
        Assert-ResultBoolean $result.$field $field
    }
    if ($result.cycles -ne 100 -or $result.completions -ne 100 -or
        $result.resident -ne 50 -or $result.stream -ne 50 -or
        $result.mp3 -ne 25 -or $result.flac -ne 25 -or
        $result.gcValueHandle -cne $true -or $result.worldScope -cne $true -or
        $result.sessionScope -cne $true -or $result.attached -cne $true -or
        $result.positioned -cne $true) {
        throw 'Managed audio acceptance result failed'
    }
    return $result
}

function Assert-BackendQueuesEmpty($Results) {
    $rows = @($Results | Where-Object event -CEQ 'rhi.backend_shutdown')
    if ($rows.Count -eq 0) { throw 'Backend shutdown result missing' }
    foreach ($row in $rows) {
        $data = $row.data
        if ($data.backend -cnotin @('DX12', 'Vulkan')) { throw 'Unknown backend result' }
        Assert-ResultBoolean $data.clean 'clean'
        foreach ($field in @('pendingTasks', 'pendingBatches', 'pendingRetirements')) {
            Assert-ResultInteger $data.$field $field
        }
        if ($data.clean -cne $true -or $data.pendingTasks -ne 0 -or
            $data.pendingBatches -ne 0 -or $data.pendingRetirements -ne 0) {
            throw 'Backend shutdown left pending work'
        }
    }
}

function Assert-EditorFinalized($Results) {
    Assert-RuntimeFinalized $Results
    Assert-BackendQueuesEmpty $Results
    $editor = Get-RuntimeResult $Results 'editor.finalized'
    $jobs = Get-RuntimeResult $Results 'jobs.shutdown'
    if ($editor.sequence -ge $jobs.sequence) { throw 'Editor finalized after scheduler shutdown' }
    return $editor
}

function Test-RuntimeResultPath($Actual, [string]$Expected) {
    if ($Actual -isnot [string] -or ![IO.Path]::IsPathRooted($Actual) -or ![IO.Path]::IsPathRooted($Expected)) {
        return $false
    }
    return [StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($Actual), [IO.Path]::GetFullPath($Expected))
}

function Get-AutomaticSaveShutdown($Results, [string]$Path, [bool]$Conflict, [string]$Source) {
    $finalized = Assert-EditorFinalized $Results
    $quit = Get-RuntimeResult $Results 'editor.quit_requested'
    $flushes = @($Results | Where-Object { $_.event -ceq 'autosave.flush' -and (Test-RuntimeResultPath $_.data.path $Path) })
    $saves = @($Results | Where-Object { $_.event -ceq 'autosave.completed' -and (Test-RuntimeResultPath $_.data.path $Path) })
    if ($flushes.Count -ne 1 -or $saves.Count -ne 1) { throw 'Expected one shutdown flush and save result' }
    $flush = $flushes[0]
    $save = $saves[0]
    Assert-ResultInteger $flush.data.ageMs 'ageMs'
    Assert-ResultBoolean $save.data.conflict 'conflict'
    Assert-ResultBoolean $save.data.succeeded 'succeeded'
    if ($quit.data.source -cne $Source -or $quit.sequence -ge $flush.sequence -or
        $flush.sequence -ge $save.sequence -or $save.sequence -ge $finalized.sequence -or
        $flush.data.ageMs -lt 0 -or $flush.data.ageMs -ge 400 -or
        $save.data.conflict -cne $Conflict -or $save.data.succeeded -cne (!$Conflict)) {
        throw 'Invalid automatic-save shutdown result'
    }
    return $flush.data
}

# PowerShell 5 lacks Start-Process -Environment. Restore every inherited variable
# immediately after launch, and cache the process handle before the child exits.
function Start-ResultProcess {
    param(
        [string]$Exe, [string]$WorkingDirectory, [string[]]$Arguments,
        [string]$Stdout, [string]$Stderr, [string]$Results,
        [hashtable]$Environment = @{}, [string]$WindowStyle = 'Hidden'
    )
    $values = @{} + $Environment
    $values['CREATOR_RUNTIME_RESULTS_FILE'] = [IO.Path]::GetFullPath($Results)
    if (Test-Path -LiteralPath $values['CREATOR_RUNTIME_RESULTS_FILE']) { throw 'Use a new result destination' }
    $saved = @{}
    try {
        foreach ($key in $values.Keys) {
            $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
            [Environment]::SetEnvironmentVariable($key, $values[$key], 'Process')
        }
        $process = Start-Process -FilePath $Exe -WorkingDirectory $WorkingDirectory -ArgumentList $Arguments `
            -WindowStyle $WindowStyle -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -PassThru
        $null = $process.Handle
        return $process
    }
    finally {
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
    }
}
