param([ValidateSet('Owners','Complete')][string]$Gate='Complete')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19R0'
New-Item -ItemType Directory -Force $out | Out-Null
$legacy=Get-Content (Join-Path $PSScriptRoot 'physics-legacy-files.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$remainingFiles=@($legacy | Where-Object {Test-Path -LiteralPath (Join-Path $repo $_.path)})
$projectReferences=@()
foreach($project in @('Engine/Physics/Physics.vcxproj','Engine/Physics/Physics.vcxproj.filters','Engine/SceneRuntime/SceneRuntime.vcxproj','Engine/SceneRuntime/SceneRuntime.vcxproj.filters')) {
    [xml]$xml=Get-Content (Join-Path $repo $project) -Raw -Encoding UTF8
    # Configuration names such as Debug|x64 and logical filters are not file paths.
    foreach($node in $xml.SelectNodes('//*[@Include and local-name()!="ProjectConfiguration" and local-name()!="Filter"]')) {
        if([IO.Path]::GetFileName($node.Include) -in @($legacy | ForEach-Object {[IO.Path]::GetFileName($_.path)})) {
            $projectReferences+=@{project=$project;include=$node.Include}
        }
    }
}
$pattern='\b(?:PhysicX|PhysicsManager|PhysicsManagers|RigidBodyComponent|BoxColliderComponent|SphereColliderComponent|CapsuleColliderComponent|MeshColliderComponent|TerrainColliderComponent|CharacterControllerComponent|CharacterControllerSystem|RagdollComponent)\b|\bPhysics->|\b(?:Cct_|Rigid_|Collider_)\w+'
$pattern+='|\b(?:DynamicRigidBody|StaticRigidBody|PhysicsEventCallback|PhysicsDebug|ICollider|CharacterController|CharacterMovement|RagdollLink|RagdollJoint|RagdollPhysics|ResourceBase|TriangleMeshResource|ConvexMeshResource|HeightFieldResource)\b'
$consumers=@()
foreach($root in @('Engine','ScriptCore','GameScripts','ScriptBinder','Dynamic_CPP','Editor','Player','Tools/AssetCooker')) {
    Get-ChildItem (Join-Path $repo $root) -File -Recurse | Where-Object {
        $_.Extension -in '.h','.hpp','.cpp','.cs' -and $_.FullName -notmatch '[\\/](obj|bin|Library|generated)[\\/]'
    } | ForEach-Object {
        $file=$_
        # The new API's profiler marker is a diagnostic label, not the retired CharacterMovement class.
        $hits=@(Select-String -LiteralPath $file.FullName -Pattern $pattern -CaseSensitive | Where-Object {
            $_.Line.TrimStart() -notmatch '^//' -and ($_.Line -replace '"Physics\.CharacterMovement"','""') -cmatch $pattern
        })
        if($hits.Count){$consumers+=@{path=[IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/');hits=@($hits|ForEach-Object {@{line=$_.LineNumber;text=$_.Line.Trim()}})}}
    }
}
$ownersRemoved=$remainingFiles.Count -eq 0 -and $projectReferences.Count -eq 0
$complete=$ownersRemoved -and $consumers.Count -eq 0
@{gate=$Gate;owners_removed=$ownersRemoved;complete=$complete;removed_file_count=$legacy.Count;remaining_files=$remainingFiles;project_references=$projectReferences;consumers=$consumers;limitations=@('Lexical reference audit; not AST reachability','P0 fixtures intentionally retain old schema','Existing Bin/Build libraries are stale pre-cutover products')} | ConvertTo-Json -Depth 15 | Set-Content "$out/cutover-audit.json" -Encoding utf8
Write-Output "PHYSICS_CUTOVER owners_removed=$ownersRemoved complete=$complete legacy_files=$($legacy.Count) consumer_files=$($consumers.Count)"
if(!$ownersRemoved -or ($Gate -eq 'Complete' -and !$complete)){exit 1}
