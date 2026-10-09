[CmdletBinding()]
param([string]$Stage='', [string]$ExpectedSceneGuid='16037efe-0cd6-4802-a24a-aafd56644346')

$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Kernel context-switch recording requires an Administrator PowerShell. No recording started.'
}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$Stage){$Stage=Join-Path $repo 'Build/Verification/ContactStream/M3Acceptance/Staging/Game-432a3fbd77b1462ab930bd6960704597'}
$Stage=(Resolve-Path -LiteralPath $Stage).Path
$wpr=(Get-Command wpr.exe -ErrorAction Stop).Source
$xperf=(Get-Command xperf.exe -ErrorAction Stop).Source
$status=@(& $wpr -status)
if($LASTEXITCODE -ne 0 -or ($status -join "`n") -notmatch 'WPR is not recording') {
    throw 'An existing or unknown WPR session is present. It will not be stopped.'
}
$out=Join-Path $repo ('Build/Verification/ContactStream/M3Acceptance/ETW-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out|Out-Null
$owned=$false
try {
    & $wpr -start GeneralProfile -filemode *> "$out/start.log"
    if($LASTEXITCODE -ne 0){throw 'WPR start failed; inspect start.log'}
    $owned=$true
    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -ExpectedSceneGuid $ExpectedSceneGuid -QueryStress -RequireMaximumBatch -QueryDynamic -QueryMixed -QueryProfile on -WaitForRenderedCapture -CaptureTailFrames 1000 -SmokeFrames 2000)
    $lines|Set-Content "$out/player.log" -Encoding utf8
    $match=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$match){throw 'Player gate did not produce a fresh receipt'}
    $evidence=$match.Substring($match.IndexOf('evidence=')+9)
    @{result='ETW_PLAYER_GATE_COMPLETE';evidence=$evidence;launch=(Get-Content "$evidence/launch.json" -Raw|ConvertFrom-Json);trace="$out/player.etl";performanceAccepted=$false}|ConvertTo-Json -Depth 15|Set-Content "$out/result.json" -Encoding utf8
}
finally {
    if($owned) {
        & $wpr -stop "$out/player.etl" *> "$out/stop.log"
        if($LASTEXITCODE -ne 0){throw "Owned WPR stop failed: $out/stop.log"}
    }
}
& $xperf -i "$out/player.etl" -o "$out/trace-statistics.txt" -a tracestats
if($LASTEXITCODE -ne 0){throw 'ETW trace statistics failed'}
& $xperf -i "$out/player.etl" -o "$out/thread-cswitch.txt" -a cswitch -thread
if($LASTEXITCODE -ne 0){throw 'ETW context switch analysis failed'}

$statistics=Get-Content "$out/trace-statistics.txt" -Raw
foreach($kind in @('Buffers','Events')) {
    $loss=[regex]::Match($statistics,"Total # Lost $kind\s*:\s*(\d+)")
    if(!$loss.Success -or [long]$loss.Groups[1].Value -ne 0){throw "ETW $kind loss or missing statistics"}
}

$analyzers=Join-Path $repo 'Build/Verification/ContactStream/M3Acceptance'
& "$analyzers/task-trace.exe" "$evidence/query.ceprof" | Set-Content "$out/physics-tasks.jsonl" -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'Physics task export failed'}
& "$analyzers/etw-switches.exe" "$out/player.etl" > "$out/switches.csv" 2> "$out/schema.log"
if($LASTEXITCODE -ne 0){throw 'ETW switch decoding failed'}
& python "$PSScriptRoot/verify-physics-task-split.py" "$out/physics-tasks.jsonl" "$out/task-split.json" > "$out/task-split.log"
if($LASTEXITCODE -ne 0){throw 'Product Run/Release hierarchy verification failed'}
& python "$PSScriptRoot/analyze-physics-etw.py" $out > "$out/task-cpu-analysis.log"
if($LASTEXITCODE -ne 0){throw 'ETW CPU/task correlation failed'}

"M3_PLAYER_ETW_RECORDED evidence=$out"
