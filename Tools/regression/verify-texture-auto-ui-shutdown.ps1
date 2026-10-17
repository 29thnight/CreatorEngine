[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Roots,
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$ResultFile
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture=[IO.Path]::GetFullPath($FixtureProject)
$records=@()
foreach($root in $Roots){
    $root=[IO.Path]::GetFullPath($root)
    $launch=Get-Content -LiteralPath (Join-Path $root 'launch.json') -Raw | ConvertFrom-Json
    if($launch.mode -notin @('deleteshutdown','corruptshutdown','conflictshutdown')){throw 'Expected an isolated pending-save shutdown run'}
    $project=Join-Path $root 'Project'
    if(!(Test-Path -LiteralPath (Join-Path $project '.texture-auto-ui-race'))){throw 'Isolation marker missing'}
    $receipt=Get-Content -LiteralPath (Join-Path $root 'commands.jsonl') | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object command -eq 'assets.texture.pendingprobe'
    if(@($receipt).Count -ne 1 -or $receipt.status -ne 'succeeded' -or !$receipt.data.diskUnchangedAtRequest -or $receipt.data.requestAgeMs -ge 400){throw 'Pending-save request receipt failed'}
    $exitRecord=Get-Content -LiteralPath (Join-Path $root 'exit.json') -Raw | ConvertFrom-Json
    if(!$exitRecord.exited -or $exitRecord.processId -ne $launch.processId -or $exitRecord.exitCode -ne 0){throw 'Editor did not terminate normally'}
    $source=Join-Path $project 'Assets/Textures/fixture.png'
    $sidecar=$source+'.meta'
    $log=[IO.File]::ReadAllText((Join-Path $root 'editor.out'))
    $flush=[regex]::Match($log,'(?m)^\[AUTOSAVE_FLUSH\] ageMs=(\d+) path='+[regex]::Escape($sidecar)+'\r?$')
    if(!$flush.Success -or !$log.Contains('[AUTOSAVE_CONFLICT] pending edit rejected path='+$sidecar)){throw 'Shutdown flush did not reject the conflicting pending save'}
    if($log.LastIndexOf('[SHUTDOWN] Finalize 완료') -le $flush.Index){throw 'Finalize did not follow the shutdown flush'}
    $metadata=if(Test-Path -LiteralPath $sidecar){[IO.File]::ReadAllText($sidecar)}else{''}
    $externalPreserved=switch($launch.mode){
        'deleteshutdown' {!(Test-Path -LiteralPath $sidecar)}
        'corruptshutdown' {$metadata -ceq $receipt.data.externalBytes}
        'conflictshutdown' {$metadata -match '(?m)^\s*maxDimension:\s*1\s*$' -and $metadata.Contains('33333333-3333-4333-8333-333333333333')}
    }
    $expectedScene=[IO.File]::ReadAllText((Join-Path $fixture 'Assets/Scenes/SavedFixture.creator')).Replace($fixture,$project)
    $record=[ordered]@{
        root=$root;configuration=$launch.configuration;mode=$launch.mode
        requestAgeMs=$receipt.data.requestAgeMs;flushAgeMs=[int64]$flush.Groups[1].Value
        exitCode=$exitRecord.exitCode;externalStatePreserved=$externalPreserved
        sourceUnchanged=((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath (Join-Path $repo 'Tools/regression/fixtures/browser-thumbnails/Tiny4.png')).Hash)
        savedSceneUnchanged=([IO.File]::ReadAllText((Join-Path $project 'Assets/Scenes/SavedFixture.creator')) -ceq $expectedScene)
        shutdownQueuesEmpty=($log -match 'backend shutdown generation \d+->\d+ pending task/batch/retirement 0/0/0')
        runtimeSha256=$launch.runtimeSha256
        runtimeUnchanged=($launch.runtimeSha256 -eq (Get-FileHash -LiteralPath (Join-Path $repo ('Bin/x64-'+$launch.configuration+'/Editor/CreatorEditor.runtime.dll'))).Hash)
        pendingReceipt=$receipt
    }
    foreach($entry in $record.GetEnumerator()){
        if($entry.Value -is [bool] -and !$entry.Value){throw ('Shutdown evidence check failed: '+$entry.Key)}
    }
    $records+=$record
}
$resultPath=[IO.Path]::GetFullPath($ResultFile)
New-Item -ItemType Directory -Path (Split-Path $resultPath) -Force | Out-Null
[ordered]@{receipts=$records;scope='Actual Inspector edit followed by controlled external mutation and RequestQuit; bounded runs'} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resultPath -Encoding utf8
$records | ForEach-Object {[pscustomobject]$_} | Select-Object mode,requestAgeMs,flushAgeMs,exitCode,externalStatePreserved,savedSceneUnchanged | Format-Table
