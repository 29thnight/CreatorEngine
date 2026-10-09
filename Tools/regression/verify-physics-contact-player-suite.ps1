[CmdletBinding()]
param([Parameter(Mandatory)][string]$EditorIndex,[Parameter(Mandatory)][string]$DevelopmentDistribution,[Parameter(Mandatory)][string]$ShippingDistribution)
$ErrorActionPreference='Stop'
$pwsh=(Get-Command pwsh).Source
$index=Get-Content -LiteralPath $EditorIndex -Raw|ConvertFrom-Json
if(!$index.complete -or $index.passed -ne 18){throw 'Complete Editor suite required'}
$out=Split-Path $EditorIndex
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function SaveIndex {
    $index.playerPending=@($index.rows|Where-Object developmentPlayer -eq 'pending').Count+@($index.rows|Where-Object shippingPlayer -eq 'pending').Count
    $index|ConvertTo-Json -Depth 14|Set-Content $EditorIndex -Encoding utf8
}
foreach($row in $index.rows){
    $reuse=''
    foreach($mode in @('development','shipping')){
        $field=$mode+'Player'
        if($row.$field -eq 'unsupported'){continue}
        $receiptField=$mode+'Receipt'
        if($row.$field -eq 'passed'){
            if($mode -eq 'development' -and $row.PSObject.Properties[$receiptField]){$reuse=Join-Path (Split-Path $row.$receiptField) 'Project'}
            continue
        }
        $gate=if($row.name -eq 'HierarchyDdol'){'hierarchy'}elseif($row.name -in @('SceneReload','Ddol')){'scene'}else{'stream'}
        $args=@('-EngineDistribution',$(if($mode -eq 'shipping'){$ShippingDistribution}else{$DevelopmentDistribution}),'-EditorResult',$row.editorReceipt)
        if($mode -eq 'shipping'){$args+='-Shipping'}
        if($reuse){$args+=@('-ReuseProject',$reuse)}
        $log=Join-Path $out ($row.name+'-'+$mode+'.log')
        & $pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "verify-physics-contact-$gate-player.ps1") @args *> $log
        if($LASTEXITCODE){$row.$field='failed';SaveIndex;throw "Player gate failed: $($row.name)/$mode ($log)"}
        $match=Select-String -LiteralPath $log -Pattern '^CONTACT_.*_PLAYER_OK evidence=(.+)$'|Select-Object -Last 1
        if(!$match){throw 'Fresh Player receipt absent'}
        $receipt=Join-Path $match.Matches[0].Groups[1].Value 'result.json'
        $result=Get-Content $receipt -Raw|ConvertFrom-Json
        if($result.exitCode -ne 0 -or !$result.packageImmutable -or [bool]$result.shipping -ne ($mode -eq 'shipping')){throw 'Invalid Player receipt'}
        $row.$field='passed'
        $row|Add-Member -NotePropertyName $receiptField -NotePropertyValue $receipt -Force
        $reuse=Join-Path (Split-Path $receipt) 'Project'
        SaveIndex
        "E0_PLAYER_CASE_OK case=$($row.name) configuration=$mode remaining=$($index.playerPending)"
    }
}
$index|Add-Member -NotePropertyName playerComplete -NotePropertyValue $true -Force
SaveIndex
"E0_PLAYER_SUITE_OK evidence=$out"