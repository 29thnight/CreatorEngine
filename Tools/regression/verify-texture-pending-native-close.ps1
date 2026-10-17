[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Roots,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$ResultFile
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture = [IO.Path]::GetFullPath($FixtureProject)
$records = @()
foreach ($root in $Roots) {
    $root = [IO.Path]::GetFullPath($root)
    $launch = Get-Content (Join-Path $root 'launch.json') -Raw | ConvertFrom-Json
    if ($launch.mode -notin @('nativeclose','conflictnativeclose')) { throw 'Expected a pending WM_CLOSE run' }
    $project = Join-Path $root 'Project'
    if (!(Test-Path (Join-Path $project '.texture-auto-ui-race'))) { throw 'Isolation marker missing' }
    $commands = @(Get-Content (Join-Path $root 'commands.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    if (@($commands | Where-Object status -ne 'succeeded').Count) { throw 'Editor command failed' }
    $receipt = @($commands | Where-Object command -eq 'assets.texture.pendingprobe')
    if ($receipt.Count -ne 1 -or $receipt[0].message -ne 'TEXTURE_PENDING_WM_CLOSE_POSTED') { throw 'WM_CLOSE receipt missing' }
    $data = $receipt[0].data
    if (!$data.diskUnchangedAtRequest -or $data.requestAgeMs -ge 400 -or $data.nativeMessage -ne 'WM_CLOSE') { throw 'Pending close boundary missed' }
    $exitRecord = Get-Content (Join-Path $root 'exit.json') -Raw | ConvertFrom-Json
    if (!$exitRecord.exited -or $exitRecord.processId -ne $launch.processId -or $exitRecord.exitCode -ne 0) { throw 'Editor exit failed' }
    $source = Join-Path $project 'Assets/Textures/fixture.png'
    $sidecar = $source + '.meta'
    $metadata = [IO.File]::ReadAllText($sidecar)
    $log = [IO.File]::ReadAllText((Join-Path $root 'editor.out'))
    $nativeIndex = $log.IndexOf('[SHUTDOWN] WM_CLOSE')
    $flush = [regex]::Match($log, '(?m)^\[AUTOSAVE_FLUSH\] ageMs=(\d+) path=' + [regex]::Escape($sidecar) + '\r?$')
    if ($nativeIndex -lt 0 -or !$flush.Success -or $flush.Index -le $nativeIndex -or [int64]$flush.Groups[1].Value -ge 400) { throw 'WM_CLOSE did not flush a pending save' }
    if ($log.LastIndexOf('[SHUTDOWN] Finalize 완료') -le $flush.Index -or $log.Contains('[SHUTDOWN] CLI quit')) { throw 'Native shutdown path was not isolated' }
    $conflict = $launch.mode -eq 'conflictnativeclose'
    $rejected = $log.Contains('[AUTOSAVE_CONFLICT] pending edit rejected path=' + $sidecar)
    if ($rejected -ne $conflict) { throw 'Conflict rejection did not match native close scenario' }
    $expectedDimension = if ($conflict) { 1 } else { 2 }
    $expectedScene = [IO.File]::ReadAllText((Join-Path $fixture 'Assets/Scenes/SavedFixture.creator')).Replace($fixture,$project)
    $record = [ordered]@{
        root=$root; configuration=$launch.configuration; mode=$launch.mode
        requestAgeMs=$data.requestAgeMs; flushAgeMs=[int64]$flush.Groups[1].Value; exitCode=$exitRecord.exitCode
        nativeShutdown=$true; conflictRejected=$rejected; expectedMaximumDimension=$expectedDimension
        expectedSettingsPreserved=($metadata -match ('(?m)^\s*maxDimension:\s*' + $expectedDimension + '\s*$'))
        externalBytesPreserved=(!$conflict -or $metadata -ceq $data.externalBytes)
        guidPreserved=$metadata.Contains('33333333-3333-4333-8333-333333333333')
        sourceUnchanged=((Get-FileHash $source).Hash -eq (Get-FileHash (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png')).Hash)
        savedSceneUnchanged=([IO.File]::ReadAllText((Join-Path $project 'Assets/Scenes/SavedFixture.creator')) -ceq $expectedScene)
        queuesEmpty=($log -match 'backend shutdown generation \d+->\d+ pending task/batch/retirement 0/0/0')
        runtimeUnchanged=($launch.runtimeSha256 -eq (Get-FileHash (Join-Path $repo ('Bin/x64-'+$launch.configuration+'/Editor/CreatorEditor.runtime.dll'))).Hash)
        runtimeSha256=$launch.runtimeSha256
    }
    foreach ($entry in $record.GetEnumerator()) {
        if ($entry.Key -ne 'conflictRejected' -and $entry.Value -is [bool] -and !$entry.Value) { throw ('Evidence failed: '+$entry.Key) }
    }
    $records += $record
}
$target = [IO.Path]::GetFullPath($ResultFile)
New-Item -ItemType Directory (Split-Path $target) -Force | Out-Null
[ordered]@{receipts=$records;scope='Actual Inspector input followed by controlled WM_CLOSE; not timed titlebar button clicks'} | ConvertTo-Json -Depth 8 | Set-Content $target -Encoding utf8
$records | ForEach-Object { [pscustomobject]$_ } | Select-Object configuration,mode,requestAgeMs,flushAgeMs,exitCode,expectedSettingsPreserved | Format-Table
