[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GeometryFixture,
    [string]$Configuration='Release',
    [ValidateRange(0.5,2.0)][double]$InspectorScale=0.75
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Owned Editor session required'}
$out=Join-Path $repo ('Build/Verification/ContactStream/GeometryUIReload-'+[guid]::NewGuid().ToString('N'))
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
    $geometryDocument=[IO.Path]::GetFullPath($GeometryFixture)
    $cases=@()
    $null=Command 'window.resize' @('1600','1200')
    $null=Command 'scene.new' @('PrimitiveReloadSentinel')
    foreach($kind in @('Geometry')){
        $source=$geometryDocument
        Require (Test-Path -LiteralPath $source) "Missing $kind UI fixture"
        $sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $sourceText=[IO.File]::ReadAllText($source)
        $rootName=[regex]::Match($sourceText,'(?m)^    m_name: (BodyInspectorUI-[a-f0-9]+)\r?$').Groups[1].Value
        Require ($rootName.Length -gt 0) 'Fixture root name unavailable'
        Require ($sourceText -match '(?m)^\s+kind: 3\s*$' -and $sourceText -match '(?m)^\s+geometryRevision: 2\s*$') 'Actual convex revision2 input missing'
        $caseRoot=Join-Path $out $kind
        New-Item -ItemType Directory -Force $caseRoot|Out-Null
        $working=Join-Path $caseRoot "$rootName.creator"
        Copy-Item -LiteralPath $source -Destination $working
        # Scene construction assigns the filename stem to m_sceneName.
        # Normalize only that expected metadata, preserving every authored field.
        $expected=Join-Path $caseRoot 'expected.creator'
        & python -c 'import sys,yaml;from pathlib import Path;d=yaml.safe_load(Path(sys.argv[1]).read_text(encoding="utf-8-sig"));d["m_sceneName"]=sys.argv[3];Path(sys.argv[2]).write_text(yaml.safe_dump(d,allow_unicode=True,sort_keys=False),encoding="utf-8")' $source $expected $rootName
        if($LASTEXITCODE){throw 'Expected scene metadata preparation failed'}
        SwitchScene $working
        $null=Command 'scene.select' @('UIBody')
        WaitFrames
        $null=Command 'scene.save' @($working)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') $expected $working
        if($LASTEXITCODE){throw "$kind reload lost authored dimensions"}
        Copy-Item -LiteralPath $working -Destination (Join-Path $caseRoot 'preplay.creator')
        $null=Command 'play.foreground_override' @('on')
        $null=Command 'play'
        WaitFrames
        Require ((Command 'undo.state').data.gameStart) "$kind Play did not start"
        $null=Command 'stop'
        WaitFrames
        Require (!(Command 'undo.state').data.gameStart) "$kind Stop did not return to Editor"
        $null=Command 'scene.save' @($working)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') (Join-Path $caseRoot 'preplay.creator') $working
        if($LASTEXITCODE){throw "$kind Stop lost authored dimensions"}
        SwitchScene $working
        WaitFrames
        $null=Command 'scene.save' @($working)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') $expected $working
        if($LASTEXITCODE){throw "$kind second reload mismatch"}
        Require ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -eq $sourceHash) "$kind original fixture changed"
        $cases+=@{kind=$kind;source=$source;sourceSha256=$sourceHash;sceneNameMetadata=$rootName;reload=$true;playStopRestore=$true;secondReload=$true;sourceUnchanged=$true}
    }
    @{result='PHYSICS_GEOMETRY_UI_RELOAD_OK';commands=$script:sequence;cases=$cases;editorRuntimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash;runtimeCollisionVerified=$false;playerVerified=$false}|ConvertTo-Json -Depth 10|Set-Content "$out/result.json" -Encoding utf8
    $null=Command 'quit'
    "PHYSICS_GEOMETRY_UI_RELOAD_OK evidence=$out"
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
}
