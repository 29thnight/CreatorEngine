[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage, [switch]$Shipping, [int]$TimeoutSeconds=600, [ValidateRange(2000,1000000)][int]$SmokeFrames=12000, [ValidateRange(20,600)][int]$DisplayStallSeconds=60, [switch]$SkipWindowResize)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Stage=[IO.Path]::GetFullPath($Stage)
$out=Join-Path $repo ('Build/Obj/Phase19DaggerPlayer/run-'+[guid]::NewGuid().ToString('N'))
$runtime=Join-Path $out 'Runtime'
New-Item -ItemType Directory -Force $runtime|Out-Null
function FileSet($root){
    @(Get-ChildItem -LiteralPath $root -File -Recurse|ForEach-Object {
        [IO.Path]::GetRelativePath($root,$_.FullName)+'|'+(Get-FileHash -LiteralPath $_.FullName).Hash
    }|Sort-Object)-join "`n"
}
if(Get-ChildItem -LiteralPath $Stage -Force -Recurse|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Stage contains reparse points'}
$before=FileSet $Stage
if (-not ('DaggerProbeWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DaggerProbeWindow {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
}
'@
}
$process=Start-Process "$Stage/Player.exe" -ArgumentList @("--smoke", "$SmokeFrames", "--smoke-promotions", "8") -WorkingDirectory $Stage -WindowStyle Hidden -Environment @{TEMP=$runtime;TMP=$runtime} -RedirectStandardOutput "$out/player.out" -RedirectStandardError "$out/player.err" -PassThru
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
    [DaggerProbeWindow]::GetWindowThreadProcessId($process.MainWindowHandle,[ref]$windowOwner)|Out-Null
    if($windowOwner -ne $process.Id){throw 'Unexpected Player window owner'}
    if(!$SkipWindowResize -and ![DaggerProbeWindow]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,0,0,960,540,4)){throw 'Owned Player test window resize failed'}
    do {
        $stdout=Get-Content "$out/player.out" -Raw
        if($stdout -match '\[physics.player.dagger\] (\{[^\r\n]+\})'){$probe=$Matches[1]|ConvertFrom-Json;break}
        if($process.HasExited){throw 'Player exited before real dynamic convex evidence'}
        if((Get-Date) -gt $deadline){throw 'Dagger simulation timed out'}
        Start-Sleep -Milliseconds 100
    } while($true)
    if($probe.passed -ne 14 -or $probe.failed -ne 0 -or !$probe.complete -or $probe.tick -lt 6){throw 'Dagger dynamic convex assertions failed'}
    $lastDisplayChange=Get-Date
    $displaySignature=$null
    $latestProgress=$null
    while(!$process.WaitForExit(1000)){
        $text=Get-Content "$out/player.out" -Raw
        $progress=[regex]::Matches($text,'\[player.smoke.progress\] (\{[^\r\n]+\})')
        if($progress.Count){
            $latestProgress=$progress[$progress.Count-1].Groups[1].Value|ConvertFrom-Json
            $signature="$($latestProgress.rendered)/$($latestProgress.completed)/$($latestProgress.promotions)"
            $phases=[regex]::Matches($text,'\[render\.(?:progress|pipeline\.progress|pass\.progress|forward\.progress)\] ([^\r\n]+)')
            if($phases.Count){$signature+="/"+$phases[$phases.Count-1].Groups[1].Value}
            if($signature -ne $displaySignature){$displaySignature=$signature;$lastDisplayChange=Get-Date}
        }
        if(((Get-Date)-$lastDisplayChange).TotalSeconds -gt $DisplayStallSeconds){throw 'Player render/display progress stalled after physics completion'}
        if((Get-Date) -gt $deadline){throw 'Player shutdown timed out'}
    }
    $stdout=Get-Content "$out/player.out" -Raw
    $stderr=Get-Content "$out/player.err" -Raw
    if($process.ExitCode -ne 0 -or $stderr -match '\[player.simulation.failed\]' -or $stdout -notmatch '\[runtime.text-parser\] calls=0'){
        throw 'Dagger Player fatal/exit/parser gate failed'
    }
    if($stdout -notmatch '\[player.smoke\] (\{[^\r\n]+\})'){throw 'Native completed render smoke missing'}
    $render=$Matches[1]|ConvertFrom-Json
    if(!$render.ready -or $render.frames -lt $SmokeFrames -or $render.displayPromotions -lt 8){throw 'Actual completed Game display/rotation gate failed'}
    if($stdout -notmatch '\[scene.document\] source=cooked guid=57269f09-9894-4811-9547-be358d717d3a'){throw 'Wrong cooked Scene'}
    $artifacts=@(Get-ChildItem $runtime -Recurse -File -Filter '*.cepg')
    if($artifacts.Count -ne 1 -or (Get-ChildItem $runtime -Recurse -File -Filter '*.cegeometry')){throw 'Cooked-only convex closure failed'}
    if($Shipping -and ($stdout -notmatch '\[player.service\] compiled=no enabled=no' -or (Get-ChildItem $runtime -Recurse -File -Filter endpoint.json))){throw 'Shipping service isolation failed'}
    if((FileSet $Stage) -ne $before){throw 'Player mutated its package'}
    $samples=@([regex]::Matches($stdout,'\[physics.player.dagger.sample\] (\{[^\r\n]+\})')|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})
    if($samples.Count -ne 6){throw 'Missing timed motion samples'}
    @{result='PHYSICS_DAGGER_PLAYER_OK';stage=$Stage;shipping=[bool]$Shipping;probe=$probe;samples=$samples;exitCode=$process.ExitCode;immutable=$true;completedGameDisplay=$true;render=$render;artifactSha256=(Get-FileHash $artifacts[0].FullName).Hash}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
    "PHYSICS_DAGGER_PLAYER_OK evidence=$out"
} catch {
    @{result='PHYSICS_DAGGER_PLAYER_FAILED';reason=$_.Exception.Message;stage=$Stage;probe=$probe;lastProgress=$latestProgress;completedGameDisplay=$false}|ConvertTo-Json -Depth 20|Set-Content "$out/failure.json" -Encoding utf8
    Write-Output "PHYSICS_DAGGER_PLAYER_FAILED evidence=$out"
    throw
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
}
