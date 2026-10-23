# Shared reader for the versioned command result contract. Human logs are diagnostics only.
function Write-CommandScript([string]$Path, [string[]]$Commands) {
    # PS5's Set-Content -Encoding UTF8 adds a BOM, which becomes part of the
    # first native command token. The command input contract uses UTF-8 without it.
    [IO.File]::WriteAllLines([IO.Path]::GetFullPath($Path), $Commands, [Text.UTF8Encoding]::new($false))
}

function Assert-CommandSequence($Results, [string[]]$Commands) {
    if (@($Results).Count -ne $Commands.Count) { throw 'Command result count does not match its input' }
    for ($index = 0; $index -lt $Commands.Count; $index++) {
        $expected = $Commands[$index].Trim().Split(' ')[0]
        if ($Results[$index].command -cne $expected) {
            throw "Command result order/identity mismatch at row $($index + 1): expected $expected"
        }
    }
}

function Read-CommandResults([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Command result file missing: $Path" }
    $rows = @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_ | ConvertFrom-Json })
    if ($rows.Count -eq 0) { throw "Empty command result file: $Path" }
    foreach ($row in $rows) {
        if ($row.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace($row.command) -or
            $row.status -notin @('succeeded','failed','invalid_arguments','preconditions_failed','cancelled','timed_out','internal_error')) {
            throw "Invalid terminal command result in $Path"
        }
    }
    return $rows
}
function Get-CommandResult($Results, [string]$Command) {
    $rows = @($Results | Where-Object command -eq $Command)
    if ($rows.Count -ne 1) { throw "Expected one $Command result, found $($rows.Count)" }
    return $rows[0]
}
function Get-SucceededCommand($Results, [string]$Command) {
    $row = Get-CommandResult $Results $Command
    if ($row.status -ne 'succeeded') { throw "$Command status=$($row.status) code=$($row.code): $($row.message)" }
    return $row.data
}
