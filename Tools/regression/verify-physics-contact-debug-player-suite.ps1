[CmdletBinding()]
param([Parameter(Mandatory)][string]$SourceIndex,[Parameter(Mandatory)][string]$EngineDistribution)
$ErrorActionPreference='Stop'
$pwsh=(Get-Command pwsh).Source
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=Get-Content $SourceIndex -Raw|ConvertFrom-Json
if(!$source.complete -or $source.passed -ne 18){throw 'Complete authored Editor suite required'}
$out=Join-Path $repo ('Build/Verification/ContactStream/M3Acceptance/Debug/ContactSuite-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out|Out-Null
$index=[ordered]@{configuration='Debug';status='running';sourceIndex=$SourceIndex;distribution=$EngineDistribution;passed=0;failed=0;pending=17;unsupported=@('AssemblyReload');rows=@()}
function SaveIndex {$index|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8}
SaveIndex
try {
    foreach($row in $source.rows){
        if($row.name -eq 'AssemblyReload'){continue}
        $gate=if($row.name -eq 'HierarchyDdol'){'hierarchy'}elseif($row.name -in @('SceneReload','Ddol')){'scene'}else{'stream'}
        $project=Join-Path (Split-Path $row.developmentReceipt) 'Project'
        $log="$out/$($row.name).log"
        & $pwsh -NoProfile -File "$PSScriptRoot/verify-physics-contact-$gate-player.ps1" -EngineDistribution $EngineDistribution -EditorResult $row.editorReceipt -ReuseProject $project -Configuration Debug *> $log
        if($LASTEXITCODE){throw "Debug ContactStream case failed $($row.name): $log"}
        $match=Select-String $log -Pattern '^CONTACT_.*_PLAYER_OK evidence=(.+)$'|Select-Object -Last 1
        if(!$match){throw 'Fresh Debug Player receipt absent'}
        $receipt=Join-Path $match.Matches[0].Groups[1].Value 'result.json'
        $result=Get-Content $receipt -Raw|ConvertFrom-Json
        if($result.exitCode -ne 0 -or !$result.packageImmutable -or $result.shipping){throw 'Invalid Debug Player receipt'}
        $index.rows+=@{name=$row.name;receipt=$receipt;passed=$true}
        $index.passed++
        $index.pending--
        SaveIndex
        "M3_DEBUG_CONTACT_CASE_OK case=$($row.name) remaining=$($index.pending)"
    }
    $index.status='complete'
    SaveIndex
    "M3_DEBUG_CONTACT_SUITE_OK receipt=$out/result.json"
}
catch {$index.status='failed';$index.failed=1;$index.error=$_.ToString();SaveIndex;throw}