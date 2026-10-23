[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineDistribution)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Verification/FSharp/Player-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $out 'Project'
New-Item -ItemType Directory -Force "$project/Assets/Script","$project/Assets/Scenes","$project/Assets/Models"|Out-Null
Copy-Item "$repo/Dynamic_CPP/ProjectSetting" "$project/ProjectSetting" -Recurse
Copy-Item "$repo/Dynamic_CPP/Assets/Shaders" "$project/Assets/Shaders" -Recurse
Copy-Item "$repo/Tools/regression/fixtures/PhysicsCharacterPlayer.creator*" "$project/Assets/Scenes"
$scene="$project/Assets/Scenes/PhysicsCharacterPlayer.creator"
$content=(Get-Content $scene -Raw).Replace('m_scriptType: CharacterPlayerProbe','m_scriptType: FSharpNativePlayerProbe')
[IO.File]::WriteAllText($scene,$content,[Text.UTF8Encoding]::new($false))
Copy-Item "$repo/GameScripts/FSharpNativePlayerProbe.fs" "$project/Assets/Script"
Set-Content "$project/Assets/Script/compile.order" 'FSharpNativePlayerProbe.fs' -Encoding utf8
$tool=Join-Path $EngineDistribution 'Bin/x64-Release/Tools/CreatorBuildTool/CreatorBuildTool.exe'
& $tool select-engine --engine-distribution $EngineDistribution --project $project *> "$out/select.log"
if($LASTEXITCODE){throw 'F# fixture engine selection failed'}
& $tool package-game --repository $repo --config Release --project $project --stage-root "$out/Staging" --input-mode Project --startup-scene PhysicsCharacterPlayer.creator --engine-distribution $EngineDistribution --smoke-frames 360 --smoke-timeout-sec 300 *> "$out/package.log"
if($LASTEXITCODE){throw "F# package failed: $out/package.log"}
$pointer=Get-Content "$out/Staging/Project.current.json" -Raw|ConvertFrom-Json
if($pointer.verification -ne 'passed'){throw 'F# package smoke failed'}
$stage=Join-Path "$out/Staging" $pointer.releaseDirectory
# Package frame-count smoke does not guarantee 30 real fixed ticks. Run an owned
# command-service session until the managed/native completion marker appears.
$runtime=Join-Path $out 'Runtime';New-Item -ItemType Directory -Force $runtime|Out-Null
$before=@{};Get-ChildItem $stage -Recurse -File|ForEach-Object {$before[$_.FullName]=(Get-FileHash $_.FullName).Hash}
$p=Start-Process (Join-Path $stage 'Player.exe') -ArgumentList @('--command-service') -WorkingDirectory $stage -WindowStyle Hidden -Environment @{TEMP=$runtime;TMP=$runtime} -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
try {
 $deadline=[DateTime]::UtcNow.AddSeconds(90);$info=$null;$ok=$null
 do {
  if($p.HasExited){throw "F# Player exited before tick marker: $($p.ExitCode)"}
  $ok=Select-String -LiteralPath "$out/player.out" -Pattern '\[FSHARP_PLAYER_OK\]' -ErrorAction SilentlyContinue
  foreach($endpoint in Get-ChildItem $runtime -Recurse -Filter endpoint.json){$e=Get-Content $endpoint.FullName -Raw|ConvertFrom-Json;if($e.pid -eq $p.Id){$info=$e}}
  if(!$ok -or !$info){Start-Sleep -Milliseconds 150}
 }until(($ok -and $info) -or [DateTime]::UtcNow -ge $deadline)
 if(!$ok -or !$info){throw "F# native tick marker/owned endpoint missing: $out"}
 $headers=@{Authorization='Bearer '+$info.token};$base='http://127.0.0.1:'+$info.port
 $null=Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType application/json -Body '{"command":"quit","args":[],"mode":"sync"}'
 if(!$p.WaitForExit(30000) -or $p.ExitCode -ne 0){throw 'F# Player graceful shutdown failed'}
 foreach($file in Get-ChildItem $stage -Recurse -File){if(!$before.ContainsKey($file.FullName) -or $before[$file.FullName] -ne (Get-FileHash $file.FullName).Hash){throw 'F# package changed during runtime acceptance'}}
 if(@(Get-ChildItem $stage -Recurse -File).Count -ne $before.Count){throw 'F# package file set changed'}
} finally {if(!$p.HasExited){$p.Kill();$p.WaitForExit()}}
@{result='FSHARP_PLAYER_PRODUCT_OK';stage=$stage;evidence=$out;scope='Release primitive CCT cooked Player; F# sole primary, fixed tick/native query/read/write';marker=@($ok|ForEach-Object {$_.Line})}|ConvertTo-Json -Depth 5|Set-Content "$out/result.json" -Encoding utf8
"FSHARP_PLAYER_PRODUCT_OK evidence=$out"
