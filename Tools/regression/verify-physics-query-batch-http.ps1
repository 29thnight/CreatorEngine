param([string]$EditorExe='', [string]$ScenePath='')
$ErrorActionPreference='Stop'
$lines=@(& "$PSScriptRoot/verify-physics-b2-http.ps1" -EditorExe $EditorExe -ScenePath $ScenePath)
$line=$lines|Where-Object {$_ -like 'PHYSICS_B2_EDITOR_OK evidence=*'}|Select-Object -Last 1
if(!$line){throw 'Missing Editor baseline receipt'}
$evidence=$line.Substring($line.IndexOf('evidence=')+9)
$stdout=Get-Content "$evidence/editor.out" -Raw
$probes=@([regex]::Matches($stdout,'\[physics.player.batch\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
# First two Play cycles contain all three body kinds. The third cycle tests failure restoration.
if($probes.Count -lt 6 -or ($probes|Select-Object -First 6|Where-Object {$_.passed -ne 16 -or $_.failed -ne 0 -or !$_.complete})){throw 'Managed Editor batch assertions missing or failed'}
$result=Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json
$result|Add-Member batchProbes @($probes|Select-Object -First 6)
$result|Add-Member queryBatchApiVersion 32
$result|ConvertTo-Json -Depth 20|Set-Content "$evidence/query-batch-result.json" -Encoding utf8
"PHYSICS_QUERY_BATCH_EDITOR_OK evidence=$evidence"
