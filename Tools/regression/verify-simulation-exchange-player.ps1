[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution,[Parameter(Mandatory)][string]$EditorResult,[ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$editor=Get-Content $EditorResult -Raw|ConvertFrom-Json
if($editor.result -ne 'N3_EXCHANGE_EDITOR_OK'){throw 'Passed owned Editor fixture required'}
$out=Join-Path $repo ('Build/Verification/N3Exchange/Player-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Scenes","$project/Assets/Script","$project/Assets/Shaders"|Out-Null
Get-ChildItem "$repo/Dynamic_CPP/Assets/Shaders"|Copy-Item -Destination "$project/Assets/Shaders" -Recurse
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item $editor.scene "$project/Assets/Scenes/N3Exchange.creator"
Copy-Item ($editor.scene+'.meta') "$project/Assets/Scenes/N3Exchange.creator.meta"
if((Get-FileHash "$project/Assets/Scenes/N3Exchange.creator").Hash -ne $editor.sceneSha256){throw 'Fixture identity changed'}
foreach($script in @('SimulationExchangeContractProbe')) {
    Copy-Item "$repo/GameScripts/$script.fs" "$project/Assets/Script"
}
Set-Content "$project/Assets/Script/compile.order" 'SimulationExchangeContractProbe.fs' -Encoding utf8
$tool=Join-Path $EngineDistribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
& $tool select-engine --engine-distribution $EngineDistribution --project $project *> "$out/select.log"
if($LASTEXITCODE){throw "Engine selection failed: $out/select.log"}
& $tool package-game --repository $repo --engine-distribution $EngineDistribution --project $project --config $Configuration --stage-root "$out/Staging" --startup-scene N3Exchange.creator --smoke-frames 120 --smoke-timeout-sec 300 *> "$out/package.log"
if($LASTEXITCODE){throw "Cooked package failed: $out/package.log"}
$pointer=Get-Content "$out/Staging/Project.current.json" -Raw|ConvertFrom-Json
if($pointer.verification -ne 'passed'){throw 'Package smoke did not pass'}
$stage=Join-Path "$out/Staging" $pointer.releaseDirectory
function Inputs([string]$root) {
    (Get-ChildItem $root -Recurse -File|Sort-Object FullName|ForEach-Object {$_.FullName.Substring($root.Length)+':'+(Get-FileHash $_.FullName).Hash}) -join "`n"
}
$before=Inputs $stage
$temp=Join-Path $out 'Runtime';New-Item -ItemType Directory -Force $temp|Out-Null
$process=Start-Process "$stage/Player.exe" -ArgumentList '--smoke','2000','--smoke-promotions','8' -WorkingDirectory $stage -WindowStyle Hidden -Environment @{TEMP=$temp;TMP=$temp} -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
try {
    if(!$process.WaitForExit(300000)){throw 'Player timeout'}
    if($process.ExitCode){throw "Player failed $($process.ExitCode): $out/player.err"}
    $stdout=Get-Content "$out/player.out" -Raw
    if([regex]::Matches($stdout,'\[N3_PRODUCT_OK\] completed=1 ').Count -ne 1 -or $stdout -match 'completed=2 '){throw 'Fresh Player N3 assertions incomplete'}
    $smoke=@([regex]::Matches($stdout,'\[player\.smoke\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    if($smoke.Count -ne 1 -or !$smoke[0].ready -or $smoke[0].frames -lt 2000 -or $smoke[0].displayPromotions -lt 8){throw 'Live Player display smoke missing'}
    if((Inputs $stage) -ne $before){throw 'Player mutated cooked inputs'}
    @{result='N3_EXCHANGE_PLAYER_OK';exitCode=$process.ExitCode;stage=$stage;editorResult=$EditorResult;smoke=$smoke[0];packageImmutable=$true;playerHash=(Get-FileHash "$stage/Player.runtime.dll").Hash}|ConvertTo-Json -Depth 15|Set-Content "$out/result.json" -Encoding utf8
    "N3_EXCHANGE_PLAYER_OK evidence=$out"
} finally {if(!$process.HasExited){$process.Kill();$process.WaitForExit()}}
