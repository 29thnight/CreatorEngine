#Requires -Version 7.0
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$ExtendedCases,
    [switch]$OnlyLive,
    [switch]$DeferRetention,
    [string]$Python='python',
    [ValidateRange(1,600)][int]$FrameTimeoutSeconds=120,
    [ValidateRange(60,2400)][int]$TimeoutSeconds=1200
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/ArtifactRetention.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Assert-ArtifactCapacity $repo
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close existing Editor before isolated verification.'}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh verification directory.'}
New-Item -ItemType Directory -Path $out|Out-Null
$project=Join-Path $out 'Project'
Copy-Item -LiteralPath $FixtureProject -Destination $project -Recurse
$shaderTarget=[IO.Path]::GetFullPath((Join-Path $project 'Assets/Shaders/DefaultPassShader'))
if(!$shaderTarget.StartsWith($out+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Private shader path escaped verification root.'}
if(Test-Path -LiteralPath $shaderTarget){Remove-Item -LiteralPath $shaderTarget -Recurse -Force}
New-Item -ItemType Directory -Force (Split-Path $shaderTarget)|Out-Null
Copy-Item -LiteralPath "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Destination $shaderTarget -Recurse
$exe="$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
function Snapshot {
    $paths=@(Get-ChildItem "$repo/Engine","$repo/Editor","$repo/Dynamic_CPP/Assets/Shaders" -Recurse -File|Where-Object {$_.Extension -in '.cpp','.h','.inl','.slang','.vcxproj','.filters'}|Sort-Object FullName|ForEach-Object FullName)
    $paths+=@($exe,"$repo/Bin/x64-$Configuration/Editor/CreatorEditor.runtime.dll",$PSCommandPath,"$PSScriptRoot/verify-meteor-scene-capture.py")
    @($paths|ForEach-Object {@{path=[IO.Path]::GetRelativePath($repo,$_);sha256=(Get-FileHash -LiteralPath $_).Hash}})
}
$before=Snapshot
$before|ConvertTo-Json -Depth 5|Set-Content "$out/identity-before.json"
$tests=@('dx12.meshletcontract','dx12.skinning','dx12.rendergraph')
if($OnlyLive){$tests=@()}
$commands=$tests+@('scene.new MeteorCutover','object.create ProbeCamera','component.add ProbeCamera CameraComponent',
    'object.transform ProbeCamera 0 3 -8 12 0 0','object.create ProbeLight Light','object.transform ProbeLight 0 20 0 50 -30 0')
# Exercise the production METEOR first writer before any material or geometry exists.
$commands+=@("render.live.ready $FrameTimeoutSeconds")
$commands+=@("render.live.capture $($out.Replace('\','/'))/live-empty editor",
    "assets.model.reimport $($project.Replace('\','/'))/Assets/Models/Prim_Suzanne.glb",
    'model.place Prim_Suzanne',
    'camera.editor match')
# Wait for a newer matching completed GPU view, not a fixed number of consumed
# CPU packets: material preparation can defer rendering while packets keep flowing.
$commands+=@("render.live.ready $FrameTimeoutSeconds")
# The reimport may invalidate the resolver while a cold generation loads.
# This renderer gate checks cached identity after the placed model's GPU view
# completed. Immediate cold-load admission is a separate asset lifecycle gate.
$commands+=@("model.loadcached $($project.Replace('\','/'))/Assets/Models/Prim_Suzanne.glb")
$commands+=@('dx12.live status',"render.live.capture $($out.Replace('\','/'))/live editor",'dx12.validation','quit')
[IO.File]::WriteAllText("$out/commands.txt",($commands -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
$start=[Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory=Split-Path $exe
$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
foreach($arg in @('--development-project',$project,'--smoke-offscreen','--commandlet-script',"$out/commands.txt",'--result-format','jsonl','--result-file',"$out/results.jsonl")){$start.ArgumentList.Add($arg)}
$start.Environment['CREATOR_DX12_VALIDATION']='gpu'
$start.Environment['CREATOR_EDITOR_WORKSPACE_DIR']="$out/workspace"
$start.Environment['CREATOR_EDITOR_LEGACY_INI']="$out/workspace/legacy.ini"
if($ExtendedCases){$start.Environment['CREATOR_METEOR_SHADOW_CASE_SUITE']='1'}else{$start.Environment.Remove('CREATOR_METEOR_SHADOW_CASE_SUITE')|Out-Null}
$start.Environment.Remove('CREATOR_METEOR_SHADOW_CAPTURE_DIR')|Out-Null
$proc=[Diagnostics.Process]::Start($start)
$stdout=$proc.StandardOutput.ReadToEndAsync();$stderr=$proc.StandardError.ReadToEndAsync()
try{
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while(!$proc.WaitForExit(1000)){if([DateTime]::UtcNow -gt $deadline){throw 'METEOR cutover verification timeout.'}}
}finally{
    if(!$proc.HasExited){$proc.Kill();$proc.WaitForExit()}
    [IO.File]::WriteAllText("$out/stdout.log",$stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText("$out/stderr.log",$stderr.GetAwaiter().GetResult())
}
$after=Snapshot;$after|ConvertTo-Json -Depth 5|Set-Content "$out/identity-after.json"
$stable=($before|ConvertTo-Json -Depth 5 -Compress) -eq ($after|ConvertTo-Json -Depth 5 -Compress)
$rows=@(Get-Content "$out/results.jsonl"|ForEach-Object {$_|ConvertFrom-Json})
$validation=@($rows|Where-Object command -eq 'dx12.validation')
$skin=@($rows|Where-Object command -eq 'dx12.skinning')
$listLines=if($skin.Count){@($skin[0].data.log -split "`n"|Where-Object {$_ -match '^SHADOW_LIST '})}else{@()}
$expected=if($OnlyLive){0}elseif($ExtendedCases){144}else{12}
$lists=$listLines.Count -eq $expected
foreach($line in $listLines){if($line -notmatch ' missing=0 extra=\d+$'){$lists=$false}}
$payloadLines=if($skin.Count){@($skin[0].data.log -split "`n"|Where-Object {$_ -match '^MESHLET_GPU_PAYLOAD '})}else{@()}
$payloads=$payloadLines.Count -eq $expected -and @($payloadLines|Where-Object {$_ -notmatch ' schema=5 profile=2 identity=exact$'}).Count -eq 0
$ready=@($rows|Where-Object command -eq 'render.live.ready')
$readiness=$ready.Count -eq 2 -and @($ready|Where-Object {
    $_.status -ne 'succeeded' -or $_.data.completionKind -ne 'editor_gpu_frame_completed' -or
    $_.data.completedFrame -le $_.data.afterFrame
}).Count -eq 0
$passed=$readiness -and $proc.ExitCode -eq 0 -and $stable -and $rows.Count -eq $commands.Count -and
    @($rows|Where-Object status -ne 'succeeded').Count -eq 0 -and
    @($rows|Where-Object {$_.command -in $tests -and $_.data.passed -ne $true}).Count -eq 0 -and $lists -and $payloads -and
    $validation.Count -eq 1 -and $validation[0].data.layerEnabled -and $validation[0].data.mode -eq 'gpu' -and
    $validation[0].data.problems -eq 0 -and $validation[0].data.droppedMessages -eq 0
if($passed){
    & $Python "$PSScriptRoot/verify-meteor-scene-capture.py" "$out/live-empty" --empty
    $passed=$LASTEXITCODE -eq 0
}
if($passed){
    & $Python "$PSScriptRoot/verify-meteor-scene-capture.py" "$out/live"
    $passed=$LASTEXITCODE -eq 0
}
$verdict=@{passed=$passed;gpuReadiness=$readiness;configuration=$Configuration;sourceStable=$stable;exitCode=$proc.ExitCode;
    normalQuit=@($rows|Where-Object {$_.command -eq 'quit' -and $_.status -eq 'succeeded'}).Count -eq 1;
    gpuPayloadChecks=$payloadLines.Count;gpuListChecks=$listLines.Count;expectedGpuListChecks=$expected;extendedCases=[bool]$ExtendedCases;
    frameTimeoutSeconds=$FrameTimeoutSeconds;scope='LX-only surface, shadow, skinning, graph and live display; no old/new route comparison';tests=$rows}
$verdict|ConvertTo-Json -Depth 12|Set-Content "$out/verdict.json"
if(!$passed){throw "LX-only cutover verification failed: $out"}
if(!$DeferRetention){Complete-ArtifactRetention $out "lx-scene-cutover-$Configuration"}
"METEOR_SCENE_CUTOVER_OK $out"
