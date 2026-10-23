[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage,[ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Stage=[IO.Path]::GetFullPath($Stage)
$out=Join-Path $repo ('Build/Verification/N1/Player-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$runtime=Join-Path $repo "Bin/x64-$Configuration/Player/Player.runtime.dll"
$staged=Join-Path $Stage 'Player.runtime.dll'
if(!(Test-Path $staged)){throw 'N1 requires a newly cooked Player package'}
$hash=(Get-FileHash $runtime).Hash
if((Get-FileHash $staged).Hash -ne $hash){throw 'Cooked stage does not contain the current Player runtime'}
$before=@{}
foreach($f in Get-ChildItem -LiteralPath $Stage -Recurse -File){$before[[IO.Path]::GetRelativePath($Stage,$f.FullName)]=(Get-FileHash $f.FullName).Hash}
$shell=(Get-Process -Id $PID).Path
& $shell -NoProfile -File "$PSScriptRoot/verify-player-runtime-text-parser.ps1" -Stage $Stage *> "$out/player.log"
if($LASTEXITCODE){Get-Content "$out/player.log" -Tail 15;throw 'Actual parser-free cooked Player regression failed'}
$text=Get-Content "$out/player.log" -Raw
if($text -notmatch 'TextParserCalls\s*:\s*0\b' -or $text -notmatch 'DirectRymlReferences\s*:\s*0\b'){throw 'Required real Player parser/source observations missing'}
$after=@{}
foreach($f in Get-ChildItem -LiteralPath $Stage -Recurse -File){$after[[IO.Path]::GetRelativePath($Stage,$f.FullName)]=(Get-FileHash $f.FullName).Hash}
if($before.Count -ne $after.Count -or @($before.Keys|Where-Object {$before[$_] -ne $after[$_]}).Count){throw 'Cooked package was modified by the runtime parser gate'}
@{result='N1_PARSER_FREE_PLAYER_OK';stage=$Stage;configuration=$Configuration;runtimeHash=$hash;textParserCalls=0;directBackendReferences=0;packageImmutable=$true;scope='Actual fresh cooked Player bootstrap and CEDO runtime documents; parser library unlinking and server product remain separate'}|ConvertTo-Json -Depth 6|Set-Content "$out/result.json" -Encoding utf8
"N1_PARSER_FREE_PLAYER_OK evidence=$out"
