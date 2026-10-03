param([Parameter(Mandatory)][string]$Stage,[switch]$Shipping)
$ErrorActionPreference='Stop'
$lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -Shipping:$Shipping)
$line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
if(!$line){throw 'Missing Player baseline receipt'}
$evidence=$line.Substring($line.IndexOf('evidence=')+9)
$stdout=Get-Content "$evidence/player.out" -Raw
$probes=@([regex]::Matches($stdout,'\[physics.player.batch\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
if($probes.Count -ne 3 -or @($probes.role|Sort-Object -Unique).Count -ne 3 -or
   ($probes|Where-Object {$_.passed -ne 16 -or $_.failed -ne 0 -or !$_.complete})){throw 'Managed multi-query assertions missing or failed'}
$result=Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json
$result|Add-Member batchProbes $probes
$result|Add-Member queryBatchApiVersion 32
$result|ConvertTo-Json -Depth 20|Set-Content "$evidence/query-batch-result.json" -Encoding utf8
"PHYSICS_QUERY_BATCH_PLAYER_OK evidence=$evidence"
