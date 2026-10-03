[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage, [ValidatePattern('^[0-9a-fA-F-]{36}$')][string]$ExpectedSceneGuid='5703e1d4-b1f5-4a32-b047-dd351c713483', [switch]$Shipping, [switch]$Shear, [switch]$QueryBenchmark, [switch]$QueryStress, [switch]$BoundedDenseCapture, [switch]$RequireMaximumBatch, [switch]$DisableTieredCompilation, [switch]$IsolateGameThread, [switch]$RequireCpuAccounting, [ValidateSet('none','off','on')][string]$QueryProfile='none', [int]$TimeoutSeconds=600, [ValidateRange(2000,1000000)][int]$SmokeFrames=12000)
$ErrorActionPreference='Stop'
if($BoundedDenseCapture -and (!$QueryStress -or !$QueryBenchmark -or $QueryProfile -ne 'on')){throw 'Bounded dense capture requires profiled stress benchmark'}
if($RequireMaximumBatch -and !$QueryStress){throw 'Maximum batch gate requires QueryStress'}

$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Stage=[IO.Path]::GetFullPath($Stage)
$out=Join-Path $repo ('Build/Obj/Phase19B2Player/run-'+[guid]::NewGuid().ToString('N'))
$runtime=Join-Path $out 'Runtime'
New-Item -ItemType Directory -Force $runtime|Out-Null
function FileSet($root){
    @(Get-ChildItem -LiteralPath $root -File -Recurse|ForEach-Object {
        [IO.Path]::GetRelativePath($root,$_.FullName)+'|'+(Get-FileHash -LiteralPath $_.FullName).Hash
    }|Sort-Object)-join "`n"
}
if(Get-ChildItem -LiteralPath $Stage -Force -Recurse|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Stage contains reparse points'}
$before=FileSet $Stage
if (-not ('PhysicsB2ProbeWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PhysicsB2ProbeWindow {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
}
'@
}
if($IsolateGameThread -and ($QueryProfile -eq 'none' -or !$QueryBenchmark)){throw 'CPU isolation requires a gated query benchmark'}
if($IsolateGameThread -and -not ('PhysicsQueryCpuPlacement' -as [type])){
    Add-Type -Path "$PSScriptRoot/PhysicsQueryCpuPlacement.cs"
}
if($QueryProfile -ne 'none' -and !$QueryBenchmark){throw 'QueryProfile requires QueryBenchmark'}
$launchArgs=@("--smoke", "$SmokeFrames", "--smoke-promotions", "8")
if($QueryProfile -ne 'none'){$launchArgs+='--command-service'}
$benchmarkGate=if($QueryProfile -ne 'none'){Join-Path $out 'query.ready'}else{''}
$launchEnvironment=@{CE_PHYSICS_QUERY_BOUNDED_CAPTURE=$(if($BoundedDenseCapture){'1'}else{'0'});CE_PHYSICS_QUERY_STRESS=$(if($QueryStress){'1'}else{'0'});TEMP=$runtime;TMP=$runtime;CE_PHYSICS_B2_SHEAR=$(if($Shear){'1'}else{'0'});CE_PHYSICS_QUERY_BENCH=$(if($QueryBenchmark){'1'}else{'0'});CE_PHYSICS_QUERY_GATE=$benchmarkGate;CE_PHYSICS_QUERY_PROFILE=$(if($QueryProfile -eq 'on'){'1'}else{'0'})}
if($DisableTieredCompilation){$launchEnvironment["DOTNET_TieredCompilation"]="0";$launchEnvironment["COMPlus_TieredCompilation"]="0"}
$memorySamples=[Collections.Generic.List[object]]::new()
function SampleMemory([int]$blockCount=-1){
    try {
        $process.Refresh()
        if(!$process.HasExited){
            $memorySamples.Add(@{utc=[DateTime]::UtcNow.ToString('o');blocks=$blockCount;workingSet=$process.WorkingSet64;privateBytes=$process.PrivateMemorySize64;peakWorkingSet=$process.PeakWorkingSet64})
        }
    } catch [InvalidOperationException] {if(!$process.HasExited){throw}}
}
$process=Start-Process "$Stage/Player.exe" -ArgumentList $launchArgs -WorkingDirectory $Stage -WindowStyle Hidden -Environment $launchEnvironment -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
try {
    $deadline=(Get-Date).AddSeconds($TimeoutSeconds)
    $windowDeadline=(Get-Date).AddSeconds(30)
    do {
        $process.Refresh()
        if($process.HasExited){throw 'Player exited before window creation'}
        if((Get-Date) -gt $windowDeadline){throw 'Player window creation timed out'}
        Start-Sleep -Milliseconds 50
    } while($process.MainWindowHandle -eq [IntPtr]::Zero)
    [uint32]$windowOwner=0
    [PhysicsB2ProbeWindow]::GetWindowThreadProcessId($process.MainWindowHandle,[ref]$windowOwner)|Out-Null
    if($windowOwner -ne $process.Id -or ![PhysicsB2ProbeWindow]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,0,0,960,540,4)){throw 'Owned Player test window resize failed'}
    if($QueryProfile -ne 'none'){
        $endpointDeadline=(Get-Date).AddSeconds(60)
        $info=$null
        do {
            $endpoints=@(Get-ChildItem $runtime -Recurse -File -Filter endpoint.json)
            foreach($endpoint in $endpoints){
                $candidate=Get-Content $endpoint.FullName -Raw|ConvertFrom-Json
                if($candidate.pid -eq $process.Id){$info=$candidate;break}
            }
            if($info){break}
            if($process.HasExited -or (Get-Date) -gt $endpointDeadline){throw 'Owned Player endpoint missing'}
            Start-Sleep -Milliseconds 100
        }while($true)
        function ProfileCommand([string]$name,[string[]]$arguments=@()){
            $body=@{command=$name;args=@($arguments);mode='sync';correlationId=[guid]::NewGuid().ToString('N')}|ConvertTo-Json -Compress
            $reply=Invoke-RestMethod "http://127.0.0.1:$($info.port)/command" -Headers @{Authorization="Bearer $($info.token)"} -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
            if($reply.operationId){
                do{Start-Sleep -Milliseconds 50;$reply=Invoke-RestMethod "http://127.0.0.1:$($info.port)$($reply.poll)" -Headers @{Authorization="Bearer $($info.token)"}}until($reply.state -eq 'completed')
            }
            if($reply.status -ne 'succeeded'){throw "$name failed: $($reply.code)"}
            @{command=$name;result=$reply}|ConvertTo-Json -Depth 20 -Compress|Add-Content "$out/profile-commands.jsonl"
            return $reply
        }
        $state=ProfileCommand $(if($QueryProfile -eq 'on'){'profile.record'}else{'profile.pause'})
        if([bool]$state.data.recording -ne ($QueryProfile -eq 'on')){throw 'Profiler state mismatch'}
        if($IsolateGameThread){
            $process.Refresh()
            [uint64]$allowed=$process.ProcessorAffinity.ToInt64()
            [uint64]$reserved=[PhysicsQueryCpuPlacement]::ReserveCore($allowed)
            [uint64]$ownerMask=$reserved -band (-bnot ($reserved-1))
            [uint64]$others=$allowed -band (-bnot $reserved)
            [uint32]$ownerThread=[PhysicsB2ProbeWindow]::GetWindowThreadProcessId($process.MainWindowHandle,[ref]$windowOwner)
            if($windowOwner -ne $process.Id){throw 'CPU fixture window ownership changed'}
            $assignments=@()
            foreach($thread in @($process.Threads)){
                [uint64]$mask=if($thread.Id -eq $ownerThread){$ownerMask}else{$others}
                $previous=[PhysicsQueryCpuPlacement]::Pin($thread.Id,$mask)
                $assignments+=@{threadId=$thread.Id;mask=$mask;previous=$previous}
            }
            if(!($assignments|Where-Object {$_.threadId -eq $ownerThread})){throw 'Owner thread not found in owned process'}
            @{ownerThread=$ownerThread;allowedMask=$allowed;reservedPhysicalCore=$reserved;ownerMask=$ownerMask;otherMask=$others;threads=$assignments}|ConvertTo-Json -Depth 10|Set-Content "$out/cpu-placement.json"
        }
        New-Item -ItemType File -Path $benchmarkGate|Out-Null
    }
    if($Shear){
        SampleMemory
    while(!$process.WaitForExit(1000)){if((Get-Date) -gt $deadline){throw 'Shear failure shutdown timed out'}}
        $stdout=[string](Get-Content "$out/player.out" -Raw)
        $stderr=Get-Content "$out/player.err" -Raw
        if($process.ExitCode -ne 3 -or $stdout -notmatch '\[physics.player.b2.shear\] requested=true' -or
            $stderr -notmatch '\[player.simulation.failed\].*unsupported shear' -or
            $stdout -notmatch '\[runtime.text-parser\] calls=0'){throw 'Required shear failure/fatal/parser gate failed'}
        if((FileSet $Stage) -ne $before){throw 'Shear failure mutated its package'}
        if($Shipping -and ($stdout -notmatch '\[player.service\] compiled=no enabled=no' -or (Get-ChildItem $runtime -Recurse -File -Filter endpoint.json))){throw 'Shipping failure service isolation failed'}
        @{result='PHYSICS_B2_PLAYER_OK';stage=$Stage;shipping=[bool]$Shipping;shear=$true;exitCode=$process.ExitCode;immutable=$true;failure=$stderr.Trim()}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
        "PHYSICS_B2_PLAYER_OK evidence=$out"
        return
    }
    do {
        SampleMemory
        $stdout=[string](Get-Content "$out/player.out" -Raw)
        $probes=@([regex]::Matches($stdout,'\[physics.player.b2\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        if($probes|Where-Object {$_.failed -ne 0}){throw 'B2 assertions failed'}
        if($probes.Count -eq 3){break}
        if($process.HasExited){throw 'Player exited before B2 evidence'}
        if((Get-Date) -gt $deadline){throw 'B2 simulation timed out'}
        Start-Sleep -Milliseconds 100
    } while($true)
    if((($probes.role|Sort-Object)-join ',') -ne 'B2Dynamic,B2Kinematic,B2Static' -or ($probes|Where-Object {$_.passed -ne 9 -or !$_.complete})){throw 'Incomplete B2 role assertions'}
    if($QueryProfile -ne 'none'){
        do{
            $queryStdout=[string](Get-Content "$out/player.out" -Raw)
            if($queryStdout -match 'query-benchmark.failure'){throw 'Profiled benchmark failed'}
            $blocks=[regex]::Matches($queryStdout,'\[physics.player.query-benchmark\] ').Count
            SampleMemory $blocks
            if($blocks -eq 16){break}
            if($process.HasExited -or (Get-Date) -gt $deadline){throw 'Profiled benchmark did not finish'}
            Start-Sleep -Milliseconds 100
        }while($true)
        if($IsolateGameThread){
            $process.Refresh()
            $liveIds=@($process.Threads|ForEach-Object {$_.Id})
            foreach($entry in $assignments){
                if($entry.threadId -in $liveIds){$null=[PhysicsQueryCpuPlacement]::Pin($entry.threadId,$entry.previous)}
            }
            @{restoredAfterBenchmark=$true;liveThreads=$liveIds}|ConvertTo-Json|Set-Content "$out/cpu-placement-restore.json"
        }
        $null=ProfileCommand 'profile.pause'
        if($QueryProfile -eq 'on'){
            $saved=ProfileCommand 'profile.save' @("$out/query.ceprof")
            if(!$saved.data.complete -or $saved.data.unacked -ne 0 -or !(Test-Path "$out/query.ceprof")){throw 'Incomplete product capture'}
        }
    }
    SampleMemory
    while(!$process.WaitForExit(1000)){if((Get-Date) -gt $deadline){throw 'Player shutdown timed out'}}
    $stdout=[string](Get-Content "$out/player.out" -Raw)
    $stderr=Get-Content "$out/player.err" -Raw
    if($process.ExitCode -ne 0 -or $stderr -match '\[player.simulation.failed\]' -or $stdout -notmatch '\[runtime.text-parser\] calls=0'){
        throw 'B2 Player fatal/exit/parser gate failed'
    }
    if($stdout -notmatch '\[player.smoke\] (\{[^\r\n]+\})'){throw 'Native completed render smoke missing'}
    $render=$Matches[1]|ConvertFrom-Json
    if(!$render.ready -or $render.frames -lt $SmokeFrames -or $render.displayPromotions -lt 8){throw 'Actual completed Game display/rotation gate failed'}
    if($stdout -notmatch ('\[scene.document\] source=cooked guid='+[regex]::Escape($ExpectedSceneGuid))){throw 'Wrong cooked Scene'}
    $artifacts=@(Get-ChildItem $runtime -Recurse -File -Filter '*.cepg')
    if($artifacts.Count -ne 1 -or (Get-ChildItem $runtime -Recurse -File -Filter '*.cegeometry')){throw 'Cooked-only convex closure failed'}
    if($Shipping -and ($stdout -notmatch '\[player.service\] compiled=no enabled=no' -or (Get-ChildItem $runtime -Recurse -File -Filter endpoint.json))){throw 'Shipping service isolation failed'}
    if($QueryBenchmark){
        $expectedSamples=if($BoundedDenseCapture){20}else{600}
        $bench=@([regex]::Matches($stdout,'\[physics.player.query-benchmark\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        if($stdout -match 'query-benchmark.failure' -or $bench.Count -ne 16 -or
           @($bench.block|Sort-Object -Unique).Count -ne 16 -or
           ($bench|Where-Object {!$_.parity -or !$_.movingCollision -or $_.samples -ne $expectedSamples -or [bool]$_.profile -ne ($QueryProfile -eq 'on') -or
                ![double]::IsFinite($_.meanUs) -or ![double]::IsFinite($_.p99Us) -or $_.meanUs -le 0 -or $_.p99Us -le 0})){
            throw 'Managed query benchmark evidence missing or failed'
        }
        if($RequireCpuAccounting){
            foreach($entry in $bench){
                if(!$entry.ownerThreadId -or $entry.rawUs.Count -ne $expectedSamples -or
                   ![double]::IsFinite($entry.blockWallUs) -or $entry.blockWallUs -le 0 -or
                   ![double]::IsFinite($entry.threadCpuUs) -or $entry.threadCpuUs -lt 0 -or $entry.threadCycles -le 0 -or
                   ($entry.rawUs|Where-Object {![double]::IsFinite($_) -or $_ -lt 0})){throw 'Invalid owner CPU accounting/raw samples'}
            }
            if(@($bench.ownerThreadId|Sort-Object -Unique).Count -ne 1){throw 'Query benchmark moved between owner threads'}
            if($IsolateGameThread -and $bench[0].ownerThreadId -ne $ownerThread){throw 'Measured query owner differs from pinned HWND owner'}
        }
        foreach($entry in $bench){
            if($BoundedDenseCapture -and (!$entry.boundedCapture -or $entry.warmup -ne 2 -or $entry.workload -ne 'dense128' -or $entry.overlapRequired -ne 128)){throw 'Bounded capture workload mismatch'}
            $expectedCount=if($entry.block -lt 8){16}else{64}
            $expectedMode=if(($entry.block % 8) -in @(1,2,5,6)){'batch'}else{'scalar'}
            if($entry.block -lt 0 -or $entry.block -gt 15 -or $entry.requests -ne $expectedCount -or $entry.mode -ne $expectedMode){throw 'Managed benchmark ABBA workload mismatch'}
        }
        if(($bench.positionX|Measure-Object -Maximum).Maximum - ($bench.positionX|Measure-Object -Minimum).Minimum -lt .01){throw 'Benchmark body did not move across physics ticks'}
        $bench|ConvertTo-Json -Depth 10|Set-Content "$out/query-benchmark.json" -Encoding utf8
    }
    if($QueryStress){
        $stress=@([regex]::Matches($stdout,'\[physics.player.stress\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
        $maximum=@([regex]::Matches($stdout,'\[physics.player.max-batch\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})

        if($stress.Count -ne 1 -or $stress[0].passed -ne 7 -or $stress[0].failed -ne 0 -or !$stress[0].complete){throw 'Density stress evidence missing or failed'}
        if($RequireMaximumBatch -and ($maximum.Count -ne 1 -or $maximum[0].passed -ne 4 -or $maximum[0].failed -ne 0 -or !$maximum[0].complete -or
           $maximum[0].requests -ne 64 -or $maximum[0].capacity -ne 4096 -or $maximum[0].written -ne 4096 -or $maximum[0].guardBytes -ne 128)){throw 'Maximum batch boundary evidence missing or failed'}

        @{stress=$stress[0];maximum=$maximum[0]}|ConvertTo-Json -Depth 8|Set-Content "$out/query-stress.json" -Encoding utf8
    }

    if((FileSet $Stage) -ne $before){throw 'Player mutated its package'}
    $memorySamples|ConvertTo-Json -Depth 8|Set-Content "$out/memory-samples.json" -Encoding utf8
    $memory=@{scope='Whole Player incl rendering/managed/physics; sampled private maximum is not lifetime private peak';samples=$memorySamples.Count;peakWorkingSet=($memorySamples.peakWorkingSet|Measure-Object -Maximum).Maximum;sampledPrivateMax=($memorySamples.privateBytes|Measure-Object -Maximum).Maximum}
    if($QueryStress -and !$memory.samples){throw 'Stress memory evidence missing'}
    @{result='PHYSICS_B2_PLAYER_OK';memory=$memory;stage=$Stage;shipping=[bool]$Shipping;probes=$probes;exitCode=$process.ExitCode;immutable=$true;completedGameDisplay=$true;testWindow=@{width=960;height=540};render=$render;artifactSha256=(Get-FileHash $artifacts[0].FullName).Hash}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
    "PHYSICS_B2_PLAYER_OK evidence=$out"
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
