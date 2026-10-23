[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Roots,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$ResultFile
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture = [IO.Path]::GetFullPath($FixtureProject)
$records = @()
foreach ($root in $Roots) {
    $root = [IO.Path]::GetFullPath($root)
    $launch = Get-Content (Join-Path $root 'launch.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($launch.mode -notin @('nativeclose','conflictnativeclose')) { throw 'Expected a pending WM_CLOSE run' }
    $project = Join-Path $root 'Project'
    if (!(Test-Path (Join-Path $project '.texture-auto-ui-race'))) { throw 'Isolation marker missing' }
    $commands = @(Read-CommandResults (Join-Path $root 'commands.jsonl'))
    if (@($commands | Where-Object status -ne 'succeeded').Count) { throw 'Editor command failed' }
    $receipt = Get-CommandResult $commands 'assets.texture.pendingprobe'
    $data = $receipt.data
    if (!$data.diskUnchangedAtRequest -or $data.requestAgeMs -ge 400 -or $data.nativeMessage -ne 'WM_CLOSE') { throw 'Pending close boundary missed' }
    $exitRecord = Get-Content (Join-Path $root 'exit.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$exitRecord.exited -or $exitRecord.processId -ne $launch.processId -or $exitRecord.exitCode -ne 0) { throw 'Editor exit failed' }
    $source = Join-Path $project 'Assets/Textures/fixture.png'
    $sidecar = $source + '.meta'
    $metadata = [IO.File]::ReadAllText($sidecar)
    $results = Read-RuntimeResults (Join-Path $root 'runtime-results.jsonl') $launch.processId
    $conflict = $launch.mode -eq 'conflictnativeclose'
    $flush = Get-AutomaticSaveShutdown $results $sidecar $conflict 'native'
    $rejected = $conflict
    $expectedDimension = if ($conflict) { 1 } else { 2 }
    $expectedScene = [IO.File]::ReadAllText((Join-Path $fixture 'Assets/Scenes/SavedFixture.creator')).Replace($fixture,$project)
    $record = [ordered]@{
        root=$root; configuration=$launch.configuration; mode=$launch.mode
        requestAgeMs=$data.requestAgeMs; flushAgeMs=$flush.ageMs; exitCode=$exitRecord.exitCode
        nativeShutdown=$true; conflictRejected=$rejected; expectedMaximumDimension=$expectedDimension
        expectedSettingsPreserved=($metadata -match ('(?m)^\s*maxDimension:\s*' + $expectedDimension + '\s*$'))
        externalBytesPreserved=(!$conflict -or $metadata -ceq $data.externalBytes)
        guidPreserved=$metadata.Contains('33333333-3333-4333-8333-333333333333')
        sourceUnchanged=((Get-FileHash $source).Hash -eq (Get-FileHash (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png')).Hash)
        savedSceneUnchanged=([IO.File]::ReadAllText((Join-Path $project 'Assets/Scenes/SavedFixture.creator')) -ceq $expectedScene)
        queuesEmpty=$true
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
