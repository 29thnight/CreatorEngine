[CmdletBinding()]
param([string]$Configuration='Release', [ValidateRange(0.5,2.0)][double]$InspectorScale=0.75)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Owned Editor session required'}
$out=Join-Path $repo ('Build/Verification/ContactStream/SharedGeometry-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$settings=Join-Path $repo 'Dynamic_CPP/ProjectSetting/EngineSettings.asset'
$settingsBytes=[IO.File]::ReadAllBytes($settings)
[IO.File]::WriteAllText($settings,[regex]::Replace([IO.File]::ReadAllText($settings),'(?m)(^imguiScale: )[^\r\n]+',('${1}'+$InspectorScale.ToString([Globalization.CultureInfo]::InvariantCulture))),[Text.UTF8Encoding]::new($false))
$process=Start-Process $exe -ArgumentList '--command-service','--console','--allow-user-code' -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -Environment @{CREATOR_EDITOR_WORKSPACE_DIR="$out/Workspace";CREATOR_EDITOR_LEGACY_INI="$out/none.ini"} -RedirectStandardOutput "$out/editor.out" -RedirectStandardError "$out/editor.err" -PassThru
$script:sequence=0
try {
    $endpoint=Join-Path $repo 'Dynamic_CPP/Library/CommandService/endpoint.json'
    $until=(Get-Date).AddSeconds(180)
    $info=$null
    do {
        if($process.HasExited){throw 'Editor exited before endpoint'}
        if(Test-Path $endpoint){$candidate=Get-Content $endpoint -Raw|ConvertFrom-Json;if($candidate.pid -eq $process.Id){$info=$candidate}}
        if(!$info){Start-Sleep -Milliseconds 150}
    }until($info -or (Get-Date) -gt $until)
    if(!$info){throw 'Owned endpoint unavailable'}
    $base="http://127.0.0.1:$($info.port)"
    $headers=@{Authorization="Bearer $($info.token)"}
    function Command([string]$name,[string[]]$arguments=@()) {
        $script:sequence++
        $body=@{command=$name;args=@($arguments);mode='sync';correlationId="body-ui-$script:sequence"}|ConvertTo-Json -Compress
        $result=Invoke-RestMethod "$base/command" -Headers $headers -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 120
        if($result.operationId){
            $pollUrl="$base$($result.poll)"
            $deadline=(Get-Date).AddSeconds(120)
            do {Start-Sleep -Milliseconds 100;$result=Invoke-RestMethod $pollUrl -Headers $headers;if((Get-Date) -gt $deadline){throw 'Operation timeout'}}until($result.state -eq 'completed')
        }
        @{command=$name;args=$arguments;result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/commands.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.message)"}
        return $result
    }

    function WaitFrames { Start-Sleep -Milliseconds 600 }
    function Require([bool]$valid,[string]$message){if(!$valid){throw $message}}
    function SwitchScene([string]$path){
        $request=Command 'scene.switch' @($path)
        Require ([bool]$request.data.activationRequested) 'Scene activation was not requested'
        $deadline=(Get-Date).AddSeconds(120)
        do {
            WaitFrames
            $state=Command 'scene.load.status' @("$($request.data.requestId)")
            Require ((Get-Date) -lt $deadline) 'Scene activation timeout'
        }until($state.data.complete)
        Require ($state.data.state -eq 'Ready' -and $state.data.activationRequested) 'Scene activation did not reach Ready'
        WaitFrames
    }
    $id=[guid]::NewGuid().ToString('N')
    $null=Command 'scene.new' @('SharedGeometry')
    $inputFile=Join-Path $out 'tetra.txt'
    [IO.File]::WriteAllText($inputFile,'4 0 0 0 1 0 0 0 1 0 0 0 1')
    New-Item -ItemType Directory -Force (Join-Path $repo 'Dynamic_CPP/Assets/PhysicsVerification')|Out-Null
    $relativeAsset="PhysicsVerification/SharedConvex-$id.cegeometry"
    $created=Command 'geometry.create' @($relativeAsset,'convex',$inputFile)
    Require ($created.data.uuid -match '^[a-f0-9-]{36}$' -and $created.data.revision -eq 1) 'Geometry identity missing'
    Require ([bool]$created.data.catalogRegistered) 'Geometry catalog registration failed'
    $assetUuid=$created.data.uuid
    $assetPath=$created.data.path
    Require (Test-Path -LiteralPath $assetPath) 'Published geometry source missing'
    $assetHash=(Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash
    $shapeFile=Join-Path $out 'shared-shape.json'
    @(@{shapeId=1;kind=3;geometryAsset=$assetUuid;geometryRevision=1})|ConvertTo-Json -AsArray -Compress|Set-Content $shapeFile -Encoding utf8
    foreach($name in @('SharedA','SharedB')){
        $null=Command 'object.create' @($name)
        $null=Command 'component.add' @($name,'PhysicsBodyComponent')
        $null=Command 'object.property' @($name,'PhysicsBodyComponent','m_motion','2')
        $null=Command 'object.transform' @($name,$(if($name -eq 'SharedA'){'-2'}else{'2'}),'5','0')
        $bound=Command 'physics.shapes' @($name,'PhysicsBodyComponent',$shapeFile)
        Require ($bound.data.changed -and $bound.data.count -eq 1) "$name convex binding failed"
    }
    $scene=Join-Path $repo "Dynamic_CPP/Assets/Scenes/SharedGeometry-$id.creator"
    $null=Command 'scene.save' @($scene)
    $null=Command 'scene.save' @($scene)
    $sourceText=[IO.File]::ReadAllText($scene)
    Require ([regex]::Matches($sourceText,'geometryAsset: '+[regex]::Escape($assetUuid)).Count -eq 2) 'Shared geometry references missing'
    Require ([regex]::Matches($sourceText,'(?m)^\s+geometryRevision: 1\s*$').Count -eq 2) 'Shared revision references missing'
    Copy-Item -LiteralPath $scene -Destination "$out/authored.creator"
    # Match the scene construction metadata contract without masking fields.
    $expected=Join-Path $out 'expected.creator'
    & python -c 'import sys,yaml;from pathlib import Path;d=yaml.safe_load(Path(sys.argv[1]).read_text(encoding="utf-8-sig"));d["m_sceneName"]=Path(sys.argv[3]).stem;Path(sys.argv[2]).write_text(yaml.safe_dump(d,allow_unicode=True,sort_keys=False),encoding="utf-8")' "$out/authored.creator" $expected $scene
    if($LASTEXITCODE){throw 'Expected metadata preparation failed'}
    $null=Command 'scene.new' @('SharedGeometrySentinel')
    SwitchScene $scene
    $null=Command 'scene.save' @($scene)
    & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') $expected $scene
    if($LASTEXITCODE){throw 'Shared geometry reference reload failed'}
    $initial=@{}
    foreach($name in @('SharedA','SharedB')){$initial[$name]=(Command 'object.describe' @($name)).data.position}
    Copy-Item -LiteralPath $scene -Destination "$out/preplay.creator"
    $null=Command 'play.foreground_override' @('on')
    $null=Command 'play'
    Require ((Command 'undo.state').data.gameStart) 'Shared geometry Play did not start'
    $deadline=(Get-Date).AddSeconds(15)
    $moving=@{}
    do {
        Start-Sleep -Milliseconds 100
        foreach($name in @('SharedA','SharedB')){
            $position=(Command 'object.describe' @($name)).data.position
            $moving[$name]=$position
        }
    }until(($moving.SharedA[1] -lt $initial.SharedA[1]-0.05 -and $moving.SharedB[1] -lt $initial.SharedB[1]-0.05) -or (Get-Date) -gt $deadline)
    foreach($name in @('SharedA','SharedB')){Require ($moving[$name][1] -lt $initial[$name][1]-0.05) "$name cooked convex simulation did not move"}
    $null=Command 'stop'
    WaitFrames
    Require (!(Command 'undo.state').data.gameStart) 'Stop did not return to Editor'
    foreach($name in @('SharedA','SharedB')){
        $position=(Command 'object.describe' @($name)).data.position
        Require (($position|ConvertTo-Json -Compress) -eq ($initial[$name]|ConvertTo-Json -Compress)) "$name Stop pose mismatch"
    }
    $null=Command 'scene.save' @($scene)
    & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') "$out/preplay.creator" $scene
    if($LASTEXITCODE){throw 'Shared geometry Stop authoring restore failed'}
    SwitchScene $scene
    $null=Command 'scene.save' @($scene)
    & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') $expected $scene
    if($LASTEXITCODE){throw 'Shared geometry second reload failed'}
    Require ((Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash -eq $assetHash) 'Shared geometry source changed'
    @{result='PHYSICS_SHARED_GEOMETRY_HTTP_OK';commands=$script:sequence;uuid=$assetUuid;revision=1;asset=$assetPath;assetSha256=$assetHash;reload=$true;secondReload=$true;bothBodiesMoved=$true;initial=$initial;moving=$moving;stopRestore=$true;inspectorAssetSelectionVerified=$false;playerVerified=$false;sharedAllocationVerified=$false;editorRuntimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    $null=Command 'quit'
    "PHYSICS_SHARED_GEOMETRY_HTTP_OK evidence=$out"
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
}
