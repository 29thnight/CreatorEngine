[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$ResultFile
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'RuntimeResults.ps1')
. (Join-Path $PSScriptRoot 'CommandResults.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root=[IO.Path]::GetFullPath($Root)
$fixture=[IO.Path]::GetFullPath($FixtureProject)
$launch=Get-Content -LiteralPath (Join-Path $root 'launch.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($launch.mode -ne 'soak'){throw 'Expected a soak launch'}
$project=Join-Path $root 'Project'
if(!(Test-Path -LiteralPath (Join-Path $project '.texture-auto-ui-race'))){throw 'Isolation marker missing'}
$commands = @(Read-CommandResults (Join-Path $root 'commands.jsonl'))
if(@($commands | Where-Object status -ne 'succeeded').Count){throw 'An Editor command failed'}
$receipt = Get-CommandResult $commands 'assets.texture.pendingprobe'
$data = $receipt.data
if($data.elapsedMs -lt 300000 -or $data.initialUiAgeMs -ge 400 -or $data.finalMaximumDimension -ne 2 -or !$data.sourceAndSavedScenePreserved){throw 'Soak duration/initial-input/final-state receipt failed'}
if($data.normal -lt 20 -or $data.conflicts -lt 20 -or $data.deleted -lt 20 -or $data.corrupt -lt 20 -or $data.forcedFlushes -lt 20 -or $data.cycles -ne ($data.normal+$data.conflicts+$data.deleted+$data.corrupt)){throw 'Insufficient or inconsistent repeated coverage'}
$object=@($commands | Where-Object command -eq 'object.describe')
if($object.Count -ne 1 -or $object[0].status -ne 'succeeded'){throw 'Unsaved object did not survive the soak'}
$exitRecord=Get-Content -LiteralPath (Join-Path $root 'exit.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if(!$exitRecord.exited -or $exitRecord.processId -ne $launch.processId -or $exitRecord.exitCode -ne 0){throw 'Editor did not terminate normally'}
$source=Join-Path $project 'Assets/Textures/fixture.png'
$metadata=[IO.File]::ReadAllText($source+'.meta')
$results = Read-RuntimeResults (Join-Path $root 'runtime-results.jsonl') $launch.processId
$finalized = Assert-EditorFinalized $results
$expectedScene=[IO.File]::ReadAllText((Join-Path $fixture 'Assets/Scenes/SavedFixture.creator')).Replace($fixture,$project)
$inputs=@(Get-Content -LiteralPath (Join-Path $root 'inputs.json') -Raw -Encoding UTF8 | ConvertFrom-Json)
foreach($inputRecord in $inputs){
    if((Get-FileHash -LiteralPath $inputRecord.path).Hash -ne $inputRecord.sha256){throw ('Validation input drift: '+$inputRecord.path)}
}
$record=[ordered]@{
    root=$root;configuration=$launch.configuration;pendingReceipt=$receipt;unsavedObjectReceipt=$object[0]
    sourceUnchanged=((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png')).Hash)
    savedSceneUnchanged=([IO.File]::ReadAllText((Join-Path $project 'Assets/Scenes/SavedFixture.creator')) -ceq $expectedScene)
    guidPreserved=$metadata.Contains('33333333-3333-4333-8333-333333333333')
    finalMaximumDimensionTwo=($metadata -match '(?m)^\s*maxDimension:\s*2\s*$')
    finalized=($finalized.sequence -gt 0)
    shutdownQueuesEmpty=$true
    runtimeSha256=$launch.runtimeSha256
    runtimeUnchanged=($launch.runtimeSha256 -eq (Get-FileHash -LiteralPath (Join-Path $repo ('Bin/x64-'+$launch.configuration+'/Editor/CreatorEditor.runtime.dll'))).Hash)
    exitCode=$exitRecord.exitCode;inputs=$inputs;sourceDrift=0
}
foreach($entry in $record.GetEnumerator()){
    if($entry.Value -is [bool] -and !$entry.Value){throw ('Soak evidence failed: '+$entry.Key)}
}
$resultPath=[IO.Path]::GetFullPath($ResultFile)
New-Item -ItemType Directory -Path (Split-Path $resultPath) -Force | Out-Null
[ordered]@{receipts=@($record);scope='Initial actual Inspector input; subsequent queue API cycles in live Editor; five-minute bounded soak'} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding utf8
"TEXTURE_AUTOSAVE_SOAK_OK configuration=$($launch.configuration) elapsedMs=$($data.elapsedMs) cycles=$($data.cycles) exitCode=$($exitRecord.exitCode) sourceDrift=0"
