[CmdletBinding()]
param([string]$Configuration='Release', [switch]$ExtendedFields, [switch]$AllFields, [switch]$LockedInputs, [switch]$ShapeUI, [switch]$ShapeDetails, [switch]$ShapeReferences, [switch]$ShapeDimensions, [switch]$PrimitiveDimensions, [switch]$B1Closure, [switch]$LayoutOnly, [ValidateRange(0.5,2.0)][double]$InspectorScale=1.0)
$ErrorActionPreference='Stop'
if($AllFields){$ExtendedFields=$true}
if($B1Closure){$ShapeReferences=$true}
if($ShapeReferences -and !$B1Closure){$ShapeDetails=$true}
if($ShapeDetails -or $ShapeReferences -or $ShapeDimensions -or $PrimitiveDimensions){$ShapeUI=$true}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Owned Editor session required'}
$out=Join-Path $repo ('Build/Verification/ContactStream/BodyInspectorUI-'+[guid]::NewGuid().ToString('N'))
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
            $deadline=(Get-Date).AddSeconds(120)
            do {Start-Sleep -Milliseconds 100;$result=Invoke-RestMethod "$base$($result.poll)" -Headers $headers;if((Get-Date) -gt $deadline){throw 'Operation timeout'}}until($result.state -eq 'completed')
        }
        @{command=$name;args=$arguments;result=$result}|ConvertTo-Json -Depth 30 -Compress|Add-Content "$out/commands.jsonl" -Encoding utf8
        if($result.status -ne 'succeeded'){throw "$name failed: $($result.message)"}
        return $result
    }

    function WaitFrames { Start-Sleep -Milliseconds 600 }
    function Require([bool]$valid,[string]$message){if(!$valid){throw $message}}
    $null=Command 'window.resize' @('1600','1200')
    WaitFrames
    $null=Command 'scene.new' @('BodyInspectorUI')
    $null=Command 'object.create' @('UIBody')
    $null=Command 'component.add' @('UIBody','PhysicsBodyComponent')
    $null=Command 'scene.select' @('UIBody')
    $null=Command 'editor.window' @('###Editor.Inspector','focus')
    $null=Command 'editor.inspector' @('expand','on')
    $null=Command 'editor.inspector' @('width','280')
    WaitFrames
    $snapshot=(Command 'editor.inspector').data
    $fields=@($snapshot.bodies|Where-Object {$_.type -eq 'PhysicsBodyComponent' -and $_.open})
    Require ($fields.Count -eq 1 -and $fields[0].fields -eq 9 -and $fields[0].firstFieldW -gt 0) 'Body field telemetry missing'
    $scrollX=[int]($fields[0].firstFieldX+40)
    $scrollY=[int]($fields[0].firstFieldY-400)
    $null=Command 'editor.nav' @('pointer',"$scrollX","$scrollY")
    WaitFrames
    $null=Command 'editor.nav' @('wheel','-4')
    WaitFrames
    $snapshot=(Command 'editor.inspector').data
    $fields=@($snapshot.bodies|Where-Object {$_.type -eq 'PhysicsBodyComponent' -and $_.open})
    Require ($snapshot.visibleWidth -ge $snapshot.contentWidth-1) 'Input field is clipped'
    if($LayoutOnly){
        Require ($fields[0].overflow -le 1) 'Body controls overflow Inspector'
        Require ($fields[0].minFieldWidth -ge 80) 'Body input width too small'
        @{result='PHYSICS_INSPECTOR_LAYOUT_OK';scale=$InspectorScale;commands=$script:sequence;inspector=$snapshot}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
        $null=Command 'quit'
        "PHYSICS_INSPECTOR_LAYOUT_OK evidence=$out"
        return
    }
    $field=$fields[0]
    $x=[int]($field.firstFieldX+$field.firstFieldW*.25)
    $y=[int]($field.firstFieldY+$field.firstFieldH*.5)
    $scene=Join-Path $repo ('Dynamic_CPP/Assets/Scenes/'+(Split-Path $out -Leaf)+'.creator')
    $null=Command 'scene.save' @($scene)
    $null=Command 'scene.save' @($scene)
    $before=[IO.File]::ReadAllText($scene)
    $depth=(Command 'undo.state').data.editUndo
    $componentType='PhysicsBodyComponent'
    function TypeMotion([string]$value,[int]$tabs=0){
        $null=Command 'editor.nav' @('pointer',"$x","$y")
        WaitFrames
        $null=Command 'editor.nav' @('wheel','100')
        WaitFrames
        $null=Command 'editor.nav' @('wheel','-4')
        WaitFrames
        $current=(Command 'editor.inspector').data
        $first=@($current.bodies|Where-Object {$_.type -eq $componentType -and $_.open})
        Require ($first.Count -eq 1 -and $first[0].firstFieldW -gt 0) 'Current field coordinates missing'
        $inputX=[int]($first[0].firstFieldX+$first[0].firstFieldW*.25)
        $inputY=[int]($first[0].firstFieldY+$first[0].firstFieldH*.5)
        $null=Command 'editor.nav' @('pointer',"$inputX","$inputY")
        WaitFrames
        $null=Command 'editor.nav' @('press')
        WaitFrames
        $null=Command 'editor.nav' @('release')
        WaitFrames
        $active=(Command 'editor.inspector').data
        Require ($active.activeId -ne 0) 'Pointer did not activate input'
        for($tab=0;$tab -lt $tabs;$tab++){
            $previous=$active.activeId
            $null=Command 'editor.nav' @('key','tab')
            WaitFrames
            $active=(Command 'editor.inspector').data
            Require ($active.activeId -ne 0 -and $active.activeId -ne $previous) 'Tab did not activate next field'
        }
        $null=Command 'editor.nav' (@('key','end')+@('backspace')*12)
        Start-Sleep -Milliseconds 1500
        $null=Command 'editor.nav' @('text',$value)
        WaitFrames
    }
    function Blur {
        $null=Command 'editor.nav' @('pointer','5','5')
        WaitFrames
        $null=Command 'editor.nav' @('press')
        WaitFrames
        $null=Command 'editor.nav' @('release')
        WaitFrames
    }
    TypeMotion '1'
    Require ((Command 'undo.state').data.editUndo -eq $depth) 'Draft published before focus loss'
    $null=Command 'scene.save' @($scene)
    Require ([IO.File]::ReadAllText($scene) -eq $before) 'Draft changed authored motion'
    Blur
    Require ((Command 'undo.state').data.editUndo -eq $depth+1) 'UI commit did not publish one Undo'
    $null=Command 'scene.save' @($scene)
    $committed=[IO.File]::ReadAllText($scene)
    Require ($committed -match 'm_motion:\s*1(?!\d)') 'UI typing did not set kinematic motion'
    $null=Command 'undo'
    WaitFrames
    $null=Command 'scene.save' @($scene)
    Require ([IO.File]::ReadAllText($scene) -eq $before) 'UI Undo mismatch'
    $null=Command 'redo'
    WaitFrames
    $null=Command 'scene.save' @($scene)
    Require ([IO.File]::ReadAllText($scene) -eq $committed) 'UI Redo mismatch'
    TypeMotion '3'
    Blur
    Require ((Command 'undo.state').data.editUndo -eq $depth+1) 'Invalid UI input changed Undo'
    $null=Command 'scene.save' @($scene)
    Require ([IO.File]::ReadAllText($scene) -eq $committed) 'Invalid UI motion persisted'
    $fieldResults=@()
    if($ExtendedFields){
        function VerifyFieldCase($case){
            $fieldDepth=(Command 'undo.state').data.editUndo
            $fieldBefore=[IO.File]::ReadAllText($scene)
            TypeMotion $case.value $case.tabs
            Require ((Command 'undo.state').data.editUndo -eq $fieldDepth) 'Field draft changed Undo'
            Blur
            Require ((Command 'undo.state').data.editUndo -eq $fieldDepth+1) 'Field commit did not publish one Undo'
            $null=Command 'scene.save' @($scene)
            $fieldAfter=[IO.File]::ReadAllText($scene)
            Require ($fieldAfter -match $case.pattern) "UI did not edit $($case.field)"
            $null=Command 'undo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $fieldBefore) 'Field Undo mismatch'
            $null=Command 'redo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $fieldAfter) 'Field Redo mismatch'
            TypeMotion $case.invalid $case.tabs
            Blur
            Require ((Command 'undo.state').data.editUndo -eq $fieldDepth+1) 'Invalid field changed Undo'
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $fieldAfter) 'Invalid field persisted'
            return @{component=$componentType;field=$case.field;value=$case.value;invalidRejected=$true;undoRedo=$true}
        }
        $bodyCases=@(
            @{field='m_mass';tabs=1;value='2.5';pattern='m_mass:\s*2\.5';invalid='-1'},
            @{field='m_initialLinearVelocity';tabs=7;value='1, 2, 3';pattern='m_initialLinearVelocity:\s*\{x: 1, y: 2, z: 3\}';invalid='nan, 2, 3'})
        if($AllFields){
            $bodyCases+=@(
                @{field='m_gravityEnabled';tabs=2;value='false';pattern='m_gravityEnabled:\s*false';invalid='maybe'},
                @{field='m_translationLocks';tabs=3;value='5';pattern='m_translationLocks:\s*5(?!\d)';invalid='8'},
                @{field='m_rotationLocks';tabs=4;value='3';pattern='m_rotationLocks:\s*3(?!\d)';invalid='8'},
                @{field='m_linearDamping';tabs=5;value='0.5';pattern='m_linearDamping:\s*0\.5';invalid='-1'},
                @{field='m_angularDamping';tabs=6;value='0.25';pattern='m_angularDamping:\s*0\.25';invalid='-1'},
                @{field='m_initialAngularVelocity';tabs=8;value='1, 2, 3';pattern='m_initialAngularVelocity:\s*\{x: 1, y: 2, z: 3\}';invalid='1, nan, 3'})
        }
        foreach($case in $bodyCases){$fieldResults+=VerifyFieldCase $case}
        $null=Command 'object.create' @('UICharacter')
        $null=Command 'component.add' @('UICharacter','CharacterMovementComponent')
        $null=Command 'scene.select' @('UICharacter')
        WaitFrames
        $componentType='CharacterMovementComponent'
        $null=Command 'scene.save' @($scene)
        $radiusBefore=[IO.File]::ReadAllText($scene)
        $radiusDepth=(Command 'undo.state').data.editUndo
        TypeMotion '0.75'
        Require ((Command 'undo.state').data.editUndo -eq $radiusDepth) 'Radius draft changed Undo'
        Blur
        Require ((Command 'undo.state').data.editUndo -eq $radiusDepth+1) 'Radius commit did not publish one Undo'
        $null=Command 'scene.save' @($scene)
        $radiusAfter=[IO.File]::ReadAllText($scene)
        Require ($radiusAfter -match 'm_radius:\s*0\.75') 'UI radius edit missing'
        $null=Command 'undo'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $radiusBefore) 'Radius Undo mismatch'
        $null=Command 'redo'
        WaitFrames
        TypeMotion '0'
        Blur
        Require ((Command 'undo.state').data.editUndo -eq $radiusDepth+1) 'Invalid radius changed Undo'
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $radiusAfter) 'Invalid radius persisted or Redo mismatch'
        $fieldResults+=@{component='CharacterMovementComponent';field='m_radius';value='0.75';invalidRejected=$true;undoRedo=$true}
        if($AllFields){
            $characterCases=@(
                @{field='m_cylinderHeight';tabs=1;value='2';invalid='0'},
                @{field='m_contactOffset';tabs=2;value='0.125';invalid='0'},
                @{field='m_stepOffset';tabs=3;value='0.5';invalid='-1'},
                @{field='m_slopeLimitCosine';tabs=4;value='0.5';invalid='1.25'},
                @{field='m_gravity';tabs=5;value='-4';invalid='nan'},
                @{field='m_minimumDistance';tabs=6;value='0.125';invalid='-1'},
                @{field='m_acceleration';tabs=7;value='32';invalid='-1'},
                @{field='m_brakingDecay';tabs=8;value='4';invalid='-1'},
                @{field='m_jumpSpeed';tabs=9;value='8';invalid='0'},
                @{field='m_maxFallSpeed';tabs=10;value='32';invalid='0'},
                @{field='m_initialVelocity';tabs=11;value='1, 2, 3';invalid='1, 2, nan';pattern='m_initialVelocity:\s*\{x: 1, y: 2, z: 3\}'})
            foreach($case in $characterCases){
                if(!$case.pattern){$case.pattern='(?m)^\s*'+$case.field+':\s*'+[regex]::Escape($case.value)+'\s*$'}
                $fieldResults+=VerifyFieldCase $case
            }
        }
    }
    $inputLockResults=@()
    if($LockedInputs){
        $null=Command 'object.create' @('UILockCharacter')
        $null=Command 'component.add' @('UILockCharacter','CharacterMovementComponent')
        $null=Command 'scene.save' @($scene)

        function BlockedInput([string]$owner,[string]$type){
            $null=Command 'scene.select' @($owner)
            WaitFrames
            $null=Command 'editor.nav' @('pointer',"$x","$y")
            WaitFrames
            $null=Command 'editor.nav' @('wheel','100')
            WaitFrames
            $null=Command 'editor.nav' @('wheel','-4')
            WaitFrames
            $view=(Command 'editor.inspector').data
            $body=@($view.bodies|Where-Object {$_.type -eq $type -and $_.open})
            Require ($body.Count -eq 1) 'Blocked input component missing'
            $px=[int]($body[0].firstFieldX+$body[0].firstFieldW*.5)
            $py=[int]($body[0].firstFieldY+$body[0].firstFieldH*.5)
            Require ($py -gt 0 -and $py -lt 1200 -and $body[0].firstFieldW -gt 0) 'Blocked input is outside viewport'
            $null=Command 'editor.nav' @('pointer',"$px","$py")
            WaitFrames
            $null=Command 'editor.nav' @('press')
            WaitFrames
            $null=Command 'editor.nav' @('release')
            WaitFrames
            $active=(Command 'editor.inspector').data.activeId
            Require ($active -eq 0) 'Disabled input became active'
            $null=Command 'editor.nav' @('text','0.125')
            WaitFrames
            Blur
        }

        foreach($ownerCase in @(@{owner='UIBody';type='PhysicsBodyComponent'},@{owner='UILockCharacter';type='CharacterMovementComponent'})){
            Blur
            $null=Command 'object.lock' @($ownerCase.owner,'true')
            WaitFrames
            $null=Command 'scene.save' @($scene)
            $lockedDocument=[IO.File]::ReadAllText($scene)
            # Selection history is independent of property Undo; select before reading its baseline.
            $null=Command 'scene.select' @($ownerCase.owner)
            WaitFrames
            $lockedDepth=(Command 'undo.state').data.editUndo
            BlockedInput $ownerCase.owner $ownerCase.type
            Require ((Command 'undo.state').data.editUndo -eq $lockedDepth) 'Locked input changed Undo'
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $lockedDocument) 'Locked input changed document'
            $null=Command 'object.lock' @($ownerCase.owner,'false')
            WaitFrames
            $inputLockResults+=@{component=$ownerCase.type;locked=$true}
        }

        $null=Command 'scene.save' @($scene)
        Copy-Item $scene "$out/inputs-preplay.creator"
        Blur
        $null=Command 'play.foreground_override' @('on')
        $null=Command 'play'
        WaitFrames
        Require ((Command 'undo.state').data.gameStart) 'Blocked input Play did not start'
        foreach($ownerCase in @(@{owner='UIBody';type='PhysicsBodyComponent'},@{owner='UILockCharacter';type='CharacterMovementComponent'})){
            $null=Command 'scene.select' @($ownerCase.owner)
            WaitFrames
            $playDepth=(Command 'undo.state').data
            BlockedInput $ownerCase.owner $ownerCase.type
            $after=(Command 'undo.state').data
            Require ($after.editUndo -eq $playDepth.editUndo -and $after.gameUndo -eq $playDepth.gameUndo) 'Play input changed Undo'
            $inputLockResults+=@{component=$ownerCase.type;play=$true}
        }
        $null=Command 'stop'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') "$out/inputs-preplay.creator" $scene
        if($LASTEXITCODE){throw 'Blocked input Stop restore failed'}
    }
    $shapeResults=$null
    if($ShapeUI){
        $null=Command 'scene.select' @('UIBody')
        WaitFrames
        $componentType='PhysicsBodyComponent'
        $null=Command 'scene.save' @($scene)
        if($B1Closure){
            $assetId=[guid]::NewGuid().ToString('N')
            New-Item -ItemType Directory -Force (Join-Path $repo 'Dynamic_CPP/Assets/PhysicsVerification')|Out-Null
            $geometryRelative="PhysicsVerification/InspectorConvex-$assetId.cegeometry"
            $geometryInput=Join-Path $out 'convex.txt'
            [IO.File]::WriteAllText($geometryInput,'4 0 0 0 1 0 0 0 1 0 0 0 1')
            $geometryCreated=Command 'geometry.create' @($geometryRelative,'convex',$geometryInput)
            Require ($geometryCreated.data.catalogRegistered -and $geometryCreated.data.revision -eq 1) 'UI geometry creation failed'
            $geometryUuid=$geometryCreated.data.uuid
            [IO.File]::WriteAllText($geometryInput,'4 0 0 0 2 0 0 0 2 0 0 0 2')
            $geometryUpdated=Command 'geometry.update' @($geometryRelative,'convex',$geometryInput)
            Require ($geometryUpdated.data.uuid -eq $geometryUuid -and $geometryUpdated.data.revision -eq '2') 'Geometry revision identity changed'
            $geometryRevisionHash=(Get-FileHash -LiteralPath $geometryCreated.data.path).Hash
            $null=Command 'undo'
            $null=Command 'redo'
            Require ((Get-FileHash -LiteralPath $geometryCreated.data.path).Hash -eq $geometryRevisionHash) 'Geometry revision Undo/Redo mismatch'
            $null=Command 'scene.save' @($scene)
        }
        $shapeBefore=[IO.File]::ReadAllText($scene)
        $shapeDepth=(Command 'undo.state').data.editUndo
        function ShapeKey([string[]]$keys){
            $null=Command 'editor.nav' (@('key')+$keys)
            do {WaitFrames;$nav=(Command 'editor.nav').data} while($nav.keysPending -gt 0)
            return $nav
        }
        function SeekShape([string]$label){
            if($label -in @('Add Shape','Physics Shape')){TypeMotion '1'}
            if($label -eq 'Apply Shapes' -and (Command 'editor.nav').data.navWidget -eq 'Reload Shapes'){
                $nav=ShapeKey @('left')
                if($nav.navWidget -eq $label){return}
            }
            for($step=0;$step -lt 80;$step++){
                $nav=ShapeKey @('tab')
                if($nav.navWidget -eq $label){return}
            }
            throw "Shape widget not reached: $label"
        }
        $shapePoints=@{}
        function ClickShape {
            $null=Command 'editor.nav' @('pointer',"$x","$y")
            WaitFrames
            $null=Command 'editor.nav' @('wheel','-100')
            WaitFrames
            $nav=(Command 'editor.nav').data
            Require ($nav.navW -gt 0 -and $nav.navH -gt 0 -and $nav.navY -ge 0 -and $nav.navY -lt 1200) 'Shape button rectangle missing'
            $clickX=[int]($nav.navX+$nav.navW*.5)
            $clickY=[int]($nav.navY+$nav.navH*.5)
            $shapePoints[$nav.navWidget]=@($clickX,$clickY)
            $null=Command 'editor.nav' @('pointer',"$clickX","$clickY")
            WaitFrames
            $null=Command 'editor.nav' @('press')
            WaitFrames
            $null=Command 'editor.nav' @('release')
            WaitFrames
        }
        TypeMotion '1'
        SeekShape 'Add Shape'
        ClickShape
        Require ((Command 'undo.state').data.editUndo -eq $shapeDepth) 'Add draft changed Undo'
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeBefore) 'Add draft changed document'
        SeekShape 'Apply Shapes'
        ClickShape
        Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Shape Apply did not publish one Undo'
        $null=Command 'scene.save' @($scene)
        $shapeAfter=[IO.File]::ReadAllText($scene)
        Require ($shapeAfter -ne $shapeBefore) 'Shape Apply did not change document'
        $null=Command 'undo'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeBefore) 'Shape Undo mismatch'
        $null=Command 'redo'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Shape Redo mismatch'

        SeekShape 'Add Shape'
        ClickShape
        SeekShape 'Reload Shapes'
        ClickShape
        SeekShape 'Apply Shapes'
        ClickShape
        Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Reload/no-op Apply changed Undo'
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Reload did not discard Add draft'

        SeekShape 'Physics Shape'
        $null=ShapeKey @('right')
        if($ShapeDetails){
            # The first control after the expanded tree is the kind combo.
            # Operate the UI, then validate the authored document rather than
            # changing the component through an HTTP property setter.
            $null=ShapeKey @('tab')
            $null=ShapeKey @('space')
            $null=ShapeKey @('down')
            $null=ShapeKey @('enter')
            Blur
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Kind draft changed document before Apply'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Kind draft changed Undo'
            SeekShape 'Apply Shapes'
            ClickShape
            $null=Command 'scene.save' @($scene)
            $kindAfter=[IO.File]::ReadAllText($scene)
            Require ($kindAfter -ne $shapeAfter) 'Kind UI did not change shape'
            Require ($kindAfter -match '(?m)^\s+kind: 1\s*$') 'Kind UI did not persist Sphere'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) 'Kind Apply did not publish one Undo'
            $null=Command 'undo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Kind Undo mismatch'
            $null=Command 'redo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $kindAfter) 'Kind Redo mismatch'
            $null=Command 'undo'
            WaitFrames
            SeekShape 'Physics Shape'
            $null=ShapeKey @('right')
        }
        if($ShapeReferences){
            $referenceValues=@(
                @{name='contactRole';value='11111111-2222-3333-4444-555555555555'},
                @{name='geometryAsset';value='aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee'},
                @{name='geometryRevision';value='9007199254740993'}
            )
            if($B1Closure){
                $referenceValues[1].value=$geometryUuid
                $referenceValues[2].value='1'
                SeekShape 'Shape Type'
                $null=ShapeKey @('space')
                $null=ShapeKey @('down')
                $null=ShapeKey @('down')
                $null=ShapeKey @('down')
                $null=ShapeKey @('enter')
            }
            function TypeShapeReference([string]$name,[string]$value){
                SeekShape $name
                Require ((Command 'editor.nav').data.activeId -ne 0) "Reference input not active: $name"
                $null=ShapeKey @('end')
                $null=ShapeKey (@('backspace')*40)
                $null=Command 'editor.nav' @('text',$value)
                WaitFrames
                Blur
            }
            foreach($field in $referenceValues){
                SeekShape $field.name
                Require ((Command 'editor.nav').data.activeId -ne 0) "Reference input not active: $($field.name)"
                $null=ShapeKey @('end')
                $null=ShapeKey @('backspace')
                $null=Command 'editor.nav' @('text',$field.value)
                WaitFrames
                Blur
            }
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Reference draft changed document'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Reference draft changed Undo'
            SeekShape 'Apply Shapes'
            ClickShape
            $null=Command 'scene.save' @($scene)
            $referencesAfter=[IO.File]::ReadAllText($scene)
            foreach($field in $referenceValues){
                Require ($referencesAfter.Contains($field.value)) "Reference UI lost $($field.name)"
            }
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) 'Reference Apply did not publish one Undo'
            $null=Command 'undo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Reference Undo mismatch'
            $null=Command 'redo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $referencesAfter) 'Reference Redo mismatch'
            if($B1Closure){
                TypeShapeReference 'geometryRevision' '2'
                SeekShape 'Apply Shapes'
                ClickShape
                $null=Command 'scene.save' @($scene)
                $revisionAfter=[IO.File]::ReadAllText($scene)
                Require ($revisionAfter -match '(?m)^\s+geometryRevision: 2\s*$') 'UI revision switch lost'
                Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+3) 'UI revision switch Undo mismatch'
                Copy-Item $scene "$out/ActualGeometry.creator"
                $null=Command 'undo'
                WaitFrames
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $referencesAfter) 'UI revision Undo mismatch'
                $null=Command 'redo'
                WaitFrames
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $revisionAfter) 'UI revision Redo mismatch'
                foreach($bad in @(@{name='contactRole';value='bad-role'},@{name='geometryAsset';value='bad-asset'},@{name='geometryRevision';value='0'})){
                    TypeShapeReference $bad.name $bad.value
                    SeekShape 'Apply Shapes'
                    ClickShape
                    $null=Command 'scene.save' @($scene)
                    Require ([IO.File]::ReadAllText($scene) -eq $revisionAfter) "Invalid $($bad.name) changed document"
                    Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+3) "Invalid $($bad.name) changed Undo"
                    SeekShape 'Reload Shapes'
                    ClickShape
                }
                $null=Command 'undo'
                WaitFrames
            }
            $null=Command 'undo'
            WaitFrames
            SeekShape 'Physics Shape'
            $null=ShapeKey @('right')
        }
        if($ShapeDimensions){
            SeekShape 'Shape Type'
            $null=ShapeKey @('space')
            $null=ShapeKey @('down')
            $null=ShapeKey @('enter')
            function TypeShapeRadius([string]$value){
                SeekShape 'radius'
                Require ((Command 'editor.nav').data.activeId -ne 0) 'Shape radius input not active'
                $null=ShapeKey @('end')
                $null=ShapeKey (@('backspace')*12)
                $null=Command 'editor.nav' @('text',$value)
                WaitFrames
                $null=ShapeKey @('enter')
                Blur
            }
            TypeShapeRadius '0.875'
            SeekShape 'Shape Layer'
            $null=ShapeKey @('space')
            $null=ShapeKey @('down')
            $null=ShapeKey @('enter')
            Blur
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Dimension draft changed document'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Dimension draft changed Undo before Apply'
            SeekShape 'Apply Shapes'
            ClickShape
            $null=Command 'scene.save' @($scene)
            $dimensionsAfter=[IO.File]::ReadAllText($scene)
            Require ($dimensionsAfter -match '(?m)^\s+kind: 1\s*$') 'Dimension fixture is not Sphere'
            Require ($dimensionsAfter -match '(?m)^\s+radius: 0\.875\s*$') 'Radius UI value lost'
            Require ($dimensionsAfter -match '(?m)^\s+layerOverride: ["\x27]?1["\x27]?\s*$') 'Layer UI did not select project layer 1'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) 'Dimension Apply Undo mismatch'
            TypeShapeRadius '-1'
            SeekShape 'Apply Shapes'
            ClickShape
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $dimensionsAfter) 'Invalid radius changed document'
            Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) 'Invalid radius changed Undo'
            SeekShape 'Reload Shapes'
            ClickShape
            $null=Command 'undo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Dimension Undo mismatch'
            $null=Command 'redo'
            WaitFrames
            $null=Command 'scene.save' @($scene)
            Require ([IO.File]::ReadAllText($scene) -eq $dimensionsAfter) 'Dimension Redo mismatch'
            $null=Command 'undo'
            WaitFrames
            SeekShape 'Physics Shape'
            $null=ShapeKey @('right')
        }
        if($PrimitiveDimensions){
            function TypePrimitiveScalar([string]$value){
                Require ((Command 'editor.nav').data.activeId -ne 0) 'Primitive input not active'
                $null=ShapeKey @('end')
                $null=ShapeKey (@('backspace')*12)
                $null=Command 'editor.nav' @('text',$value)
                WaitFrames
                $null=ShapeKey @('enter')
                Blur
            }
            function TypeBoxAxis([int]$axis,[string]$value){
                SeekShape 'contactRole'
                for($i=0;$i -le $axis;$i++){$null=ShapeKey @('tab')}
                TypePrimitiveScalar $value
            }
            foreach($primitive in @('Box','Capsule')){
                if($primitive -eq 'Box'){
                    TypeBoxAxis 0 '0.75'
                    TypeBoxAxis 1 '1.25'
                    TypeBoxAxis 2 '2.5'
                }else{
                    SeekShape 'Shape Type'
                    $null=ShapeKey @('space')
                    $null=ShapeKey @('down')
                    $null=ShapeKey @('down')
                    $null=ShapeKey @('enter')
                    SeekShape 'radius'
                    TypePrimitiveScalar '0.625'
                    SeekShape 'halfHeight'
                    TypePrimitiveScalar '1.5'
                }
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) "$primitive draft changed document"
                Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) "$primitive draft changed Undo"
                SeekShape 'Apply Shapes'
                ClickShape
                $null=Command 'scene.save' @($scene)
                $primitiveAfter=[IO.File]::ReadAllText($scene)
                if($primitive -eq 'Box'){
                    Require ($primitiveAfter -match 'halfExtent: \{x: 0\.75, y: 1\.25, z: 2\.5\}') 'Box dimensions lost'
                }else{
                    Require ($primitiveAfter -match '(?m)^\s+kind: 2\s*$' -and $primitiveAfter -match '(?m)^\s+radius: 0\.625\s*$' -and $primitiveAfter -match '(?m)^\s+halfHeight: 1\.5\s*$') 'Capsule dimensions lost'
                }
                Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) "$primitive Apply Undo mismatch"
                Copy-Item $scene "$out/$primitive-dimensions.creator"
                if($primitive -eq 'Box'){
                    TypeBoxAxis 1 '-1'
                }else{
                    SeekShape 'halfHeight'
                    TypePrimitiveScalar '-1'
                }
                SeekShape 'Apply Shapes'
                ClickShape
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $primitiveAfter) "$primitive invalid changed document"
                Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) "$primitive invalid changed Undo"
                SeekShape 'Reload Shapes'
                ClickShape
                $null=Command 'undo'
                WaitFrames
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) "$primitive Undo mismatch"
                $null=Command 'redo'
                WaitFrames
                $null=Command 'scene.save' @($scene)
                Require ([IO.File]::ReadAllText($scene) -eq $primitiveAfter) "$primitive Redo mismatch"
                $null=Command 'undo'
                WaitFrames
                SeekShape 'Physics Shape'
                $null=ShapeKey @('right')
            }
        }
        SeekShape 'Remove Shape'
        ClickShape
        Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+1) 'Remove draft changed Undo'
        SeekShape 'Apply Shapes'
        ClickShape
        Require ((Command 'undo.state').data.editUndo -eq $shapeDepth+2) 'Remove Apply did not publish one Undo'
        $null=Command 'scene.save' @($scene)
        $shapeRemoved=[IO.File]::ReadAllText($scene)
        Require ($shapeRemoved -ne $shapeAfter) 'Remove Apply did not change document'
        $null=Command 'undo'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeAfter) 'Remove Undo mismatch'
        $null=Command 'redo'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $shapeRemoved) 'Remove Redo mismatch'
        Require ([regex]::Matches($shapeAfter,'(?m)^\s+- shapeId:').Count -eq 2) 'Add did not persist two shapes'
        Require ([regex]::Matches($shapeRemoved,'(?m)^\s+- shapeId:').Count -eq 1) 'Remove did not persist one shape'
        function ClickBlockedShapes {
            $null=Command 'editor.nav' @('pointer',"$x","$y")
            WaitFrames
            $null=Command 'editor.nav' @('wheel','-100')
            WaitFrames
            foreach($label in @('Add Shape','Apply Shapes')){
                $point=$shapePoints[$label]
                Require ($point.Count -eq 2) 'Blocked button coordinates missing'
                $null=Command 'editor.nav' @('pointer',"$($point[0])","$($point[1])")
                WaitFrames
                $null=Command 'editor.nav' @('press')
                WaitFrames
                $null=Command 'editor.nav' @('release')
                WaitFrames
            }
        }
        Blur
        $null=Command 'object.lock' @('UIBody','true')
        WaitFrames
        $null=Command 'scene.save' @($scene)
        $lockedBefore=[IO.File]::ReadAllText($scene)
        $lockedDepth=(Command 'undo.state').data.editUndo
        ClickBlockedShapes
        Require ((Command 'undo.state').data.editUndo -eq $lockedDepth) 'Locked Shape UI changed Undo'
        $null=Command 'scene.save' @($scene)
        Require ([IO.File]::ReadAllText($scene) -eq $lockedBefore) 'Locked Shape UI changed document'
        $null=Command 'object.lock' @('UIBody','false')
        WaitFrames
        $null=Command 'scene.save' @($scene)
        Copy-Item $scene "$out/preplay.creator"
        Blur
        $null=Command 'play.foreground_override' @('on')
        $null=Command 'play'
        WaitFrames
        $null=Command 'scene.select' @('UIBody')
        WaitFrames
        $playState=(Command 'undo.state').data
        Require ($playState.gameStart) 'Shape UI Play did not start'
        ClickBlockedShapes
        $playAfter=(Command 'undo.state').data
        Require ($playAfter.editUndo -eq $playState.editUndo -and $playAfter.gameUndo -eq $playState.gameUndo) 'Play Shape UI changed Undo'
        $null=Command 'stop'
        WaitFrames
        $null=Command 'scene.save' @($scene)
        & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') "$out/preplay.creator" $scene
        if($LASTEXITCODE){throw 'Shape UI Stop restore failed'}
        $shapeResults=@{addDraft=$true;apply=$true;reload=$true;removeDraft=$true;undoRedo=$true;lockedButtons=$true;playButtons=$true;stopRestore=$true;kindDetail=[bool]$ShapeDetails;referenceInputs=[bool]$ShapeReferences;dimensionInputs=[bool]$ShapeDimensions;primitiveDimensions=[bool]$PrimitiveDimensions;actualGeometryRevisionUI=[bool]$B1Closure;invalidReferencesUI=[bool]$B1Closure}
    }
    Copy-Item $scene -Destination "$out/committed.creator"
    $reloadExpected=Join-Path $out 'reload-expected.creator'
    & python -c 'import sys,yaml;from pathlib import Path;d=yaml.safe_load(Path(sys.argv[1]).read_text(encoding="utf-8-sig"));d["m_sceneName"]=Path(sys.argv[3]).stem;Path(sys.argv[2]).write_text(yaml.safe_dump(d,allow_unicode=True,sort_keys=False),encoding="utf-8")' "$out/committed.creator" $reloadExpected $scene
    if($LASTEXITCODE){throw 'Reload metadata preparation failed'}
    $reloadRequest=Command 'scene.switch' @($scene)
    Require ([bool]$reloadRequest.data.activationRequested) 'Reload activation not requested'
    $reloadDeadline=(Get-Date).AddSeconds(120)
    do {
        WaitFrames
        $reloadStatus=Command 'scene.load.status' @("$($reloadRequest.data.requestId)")
        Require ((Get-Date) -lt $reloadDeadline) 'Reload activation timeout'
    }until($reloadStatus.data.complete)
    Require ($reloadStatus.data.state -eq 'Ready') 'Reload did not reach Ready'
    WaitFrames
    $null=Command 'scene.select' @('UIBody')
    WaitFrames
    $null=Command 'scene.save' @($scene)
    & python (Join-Path $PSScriptRoot 'verify_scene_document_equivalence.py') $reloadExpected $scene
    if($LASTEXITCODE){throw 'Scene document verification failed'}
    Require ([IO.File]::ReadAllText($scene) -match 'm_motion:\s*1(?!\d)') 'Reload lost UI value'
    @{result='PHYSICS_BODY_INSPECTOR_UI_OK';scene=$scene;commands=$script:sequence;initialInspector=$snapshot;value='kinematic';invalidMotionRejected=$true;undoRedo=$true;focusLoss=$true;extendedFields=[bool]$ExtendedFields;allFields=[bool]$AllFields;inputLockResults=$inputLockResults;fieldResults=$fieldResults;shapeResults=$shapeResults}|ConvertTo-Json -Depth 20|Set-Content "$out/result.json" -Encoding utf8
    $null=Command 'quit'
    "PHYSICS_BODY_INSPECTOR_UI_OK evidence=$out"
} finally {
    if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit()}
    [IO.File]::WriteAllBytes($settings,$settingsBytes)
}
