param([Parameter(Mandatory)][string]$EvidenceFile)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Exercise the actual driver's pure acceptance predicate. Do not run its live
# commands when loading the function. Mutations start from accepted live input.
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot '../temporal-validation/Verify-TemporalHistoryRecovery.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Driver parse failed' }
$predicate = $ast.Find({param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Require-Reset'
}, $true)
if (!$predicate) { throw 'Production reset predicate missing' }
. ([scriptblock]::Create($predicate.Extent.Text))
$evidence = Get-Content -LiteralPath $EvidenceFile -Raw | ConvertFrom-Json
if (!$evidence.passed) { throw 'Use accepted fresh live evidence' }
$resets = @($evidence.records | Where-Object {$_.action -in @('camera-cut','window-resize')})
if ($resets.Count -ne 9) { throw 'Missing native/FSR/XeSS live reset matrix' }
foreach ($record in $resets) { Require-Reset $record.before $record.after $record.provider }
$source = $resets | Where-Object {$_.provider -eq 'fsr' -and $_.action -eq 'window-resize'} | Select-Object -First 1
$mutations = @(
    {param($r) $r.after.lastHistoryReset.realFrameId = $r.before.lastRealFrameId},
    {param($r) $r.after.lastHistoryReset.realFrameId = [string]([uint64]$r.after.lastRealFrameId+1)},
    {param($r) $r.after.lastHistoryReset.historyRevision = $r.before.historyRevision},
    {param($r) $r.after.lastHistoryReset.jitterX = 0.25},
    {param($r) $r.after.lastHistoryReset.jitterY = 0},
    {param($r) $r.after.lastHistoryReset.displayExtent.width += 1},
    {param($r) $r.after.lastHistoryReset.viewId = [string]([uint64]$r.after.viewId+1)},
    {param($r) $r.after.lastHistoryReset.sceneEpoch = [string]([uint64]$r.after.sceneEpoch+1)},
    {param($r) $r.after.lastHistoryReset.provider = 'none'}
)
foreach ($mutate in $mutations) {
    $copy = $source | ConvertTo-Json -Depth 25 | ConvertFrom-Json
    & $mutate $copy
    $rejected = $false
    try { Require-Reset $copy.before $copy.after $copy.provider } catch { $rejected = $true }
    if (!$rejected) { throw 'Corrupted reset evidence was accepted' }
}
"PASS: $($resets.Count) live positives and $($mutations.Count) rejected mutations"
