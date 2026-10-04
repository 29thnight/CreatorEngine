[CmdletBinding()]
param([string]$Out='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$Out){$Out=Join-Path $repo ('Build/Obj/PhysicsSdkBoundary/'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force $Out|Out-Null

# Backend implementation and the frozen pre-cutover SDK baseline are the only exceptions.
$allowed=@('Engine/Physics/PhysicsScene.cpp','Tools/regression/physics_p0_cpu_probe.cpp')
$sdkPattern='\b(?:physx\s*::|namespace\s+physx\b|Px[A-Z]\w*|PX_PHYSICS_VERSION)\b|#\s*include\s*[<"][^>"\r\n]*(?:physx[/\\]|Px[A-Z])'
function HasSdk([string]$source){$source -match $sdkPattern}

# Negative controls cover headers, exposed SDK types, and a forward declaration escape.
foreach($sample in @('#include <physx/PxPhysicsAPI.h>','physx::PxScene* scene;','namespace physx { class PxScene; }')){
    if(!(HasSdk $sample)){throw 'SDK boundary negative control missed a leak'}
}
if(HasSdk '#include "PhysicsScene.h"'){throw 'SDK-free facade falsely rejected'}

$paths=@(& git -C $repo ls-files --cached --others --exclude-standard)|Sort-Object -Unique
if($LASTEXITCODE){throw 'Source inventory failed'}
$findings=@()
$exceptions=@()
$scanned=0
$hashes=@()
$projectsScanned=0
$retiredHeaders='(?:PhysicsMathAdapter|PhysicsSystem|BoxColliderComponent|SphereColliderComponent|CapsuleColliderComponent|MeshColliderComponent|RigidBodyComponent|CharacterControllerComponent|RagdollComponent)\.h'
foreach($path in $paths){
    if($path -match '\.(?:vcxproj|props|targets)$' -and $path -notmatch '^(?:ThirdParty|vcpkg_installed|Build|Bin|ports)/'){
        $absolute=Join-Path $repo $path
        if(Test-Path -LiteralPath $absolute -PathType Leaf){
            ++$projectsScanned
            $text=Get-Content -LiteralPath $absolute -Raw
            if($text -match $retiredHeaders -or ($text -match 'include[/\\]+physx' -and $path -ne 'Directory.Build.targets')){$findings+=$path}
        }
    }
    if($path -notmatch '\.(?:h|hpp|cpp|inl|cxx|cc)$' -or $path -match '^(?:ThirdParty|vcpkg_installed|Build|Bin|ports)/'){continue}
    $absolute=Join-Path $repo $path
    if(!(Test-Path -LiteralPath $absolute -PathType Leaf)){continue}
    ++$scanned
    $hashes+=@{path=$path;sha256=(Get-FileHash -LiteralPath $absolute).Hash}
    $text=Get-Content -LiteralPath $absolute -Raw
    if($text -match ('#\s*include\s*[<"][^>"\r\n]*'+$retiredHeaders)){$findings+=$path}
    if(HasSdk $text){
        if($path -in $allowed){$exceptions+=$path}else{$findings+=$path}
    }
}
foreach($path in @('Engine/Physics/PhysicsMathAdapter.h','Engine/Physics/PhysicsSystem.h')){
    if(Test-Path -LiteralPath (Join-Path $repo $path)){$findings+=$path}
}

[xml]$targets=Get-Content (Join-Path $repo 'Directory.Build.targets') -Raw
$sdkIncludes=@($targets.SelectNodes('//*[local-name()="ItemDefinitionGroup"]')|Where-Object {$_.InnerXml -match 'include\\physx'})
if($sdkIncludes.Count -ne 1 -or $sdkIncludes[0].Condition -ne "'`$(MSBuildProjectName)' == 'Physics'"){
    throw 'SDK include directory must be scoped to Physics project only'
}
[xml]$project=Get-Content (Join-Path $repo 'Engine/Physics/Physics.vcxproj') -Raw
$references=@($project.SelectNodes('//*[local-name()="ProjectReference"]'))
if($references.Count -ne 1 -or $references[0].Include -ne '..\EngineDiagnostics\EngineDiagnostics.vcxproj'){
    throw 'Physics backend acquired an unexpected project dependency'
}

@{result=$(if($findings.Count){'PHYSICS_SDK_BOUNDARY_FAILED'}else{'PHYSICS_SDK_BOUNDARY_OK'});scanned=$scanned;projectsScanned=$projectsScanned;findings=$findings;exceptions=$exceptions;negativeControls=3;sourceHashes=$hashes}|
    ConvertTo-Json -Depth 6|Set-Content (Join-Path $Out 'result.json') -Encoding utf8
if($findings.Count){throw "SDK boundary leaks: $($findings -join ', ')"}
Write-Output "PHYSICS_SDK_BOUNDARY_OK scanned=$scanned evidence=$Out"
