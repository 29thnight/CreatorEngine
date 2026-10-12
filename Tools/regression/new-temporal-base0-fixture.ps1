#Requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture=[IO.Path]::GetFullPath($OutputDirectory)
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $fixture
if(Test-Path -LiteralPath $fixture){throw 'Use a fresh fixture directory'}
foreach($dir in @('Assets/Scenes','Assets/Models','Assets/Materials','Assets/HDR','ProjectSetting')){
 [IO.Directory]::CreateDirectory((Join-Path $fixture $dir))|Out-Null
}
foreach($pair in @(@('Models','Prim_Plane.glb'),@('Models','Prim_Plane.glb.meta'),
 @('Materials','Plane_Mat_BaseColor.png'),@('Materials','Plane_Mat_BaseColor.png.meta'),
 @('HDR','autumn_field_puresky_1k.hdr'),@('HDR','autumn_field_puresky_1k.hdr.meta'))){
 Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/Assets/$($pair[0])/$($pair[1])") `
  -Destination (Join-Path $fixture "Assets/$($pair[0])/$($pair[1])")
}
foreach($name in @('EngineSettings.asset','AssetIdentity.asset','CollisionMatrix.asset','Layers.celayers','TagManager.asset')){
 Copy-Item -LiteralPath (Join-Path $repo "Dynamic_CPP/ProjectSetting/$name") -Destination "$fixture/ProjectSetting/$name"
}
$settings=[IO.File]::ReadAllText("$fixture/ProjectSetting/EngineSettings.asset").Replace(
 'skyboxTextureName: forest.ceibl','skyboxTextureName: autumn_field_puresky_1k.hdr')
[IO.File]::WriteAllText("$fixture/ProjectSetting/EngineSettings.asset",$settings)
$entities=[Regex]::Split([IO.File]::ReadAllText((Join-Path $repo 'Dynamic_CPP/Assets/Scenes/FT_Primitives.creator')),
 '(?m)(?=^  - Entity:)')
if($entities.Count -lt 5){throw 'Authored primitive scene layout changed'}
$scene=($entities[0..4]-join '').Replace('m_name: FT_Primitives','m_name: TR0_Native_Workload').Replace(
 'm_childrenIndices: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]','m_childrenIndices: [1, 2, 3]').Replace(
 'm_isPrimary: false','m_isPrimary: true')
$scene=[Regex]::Replace($scene,'(?ms)^      - ScriptComponent:.*?(?=^    m_isStatic:)','')
$scene=$scene.Replace('        m_Material:',"        m_modelGuid: 8c05d8c6-f8bb-8ebb-8b1d-90e2d3b66e45`n        m_Material:")
$scene=$scene.Replace('m_baseColorTexName: Plane_Mat_BaseColor.png','m_baseColorTexName: ""')
$scene=[Regex]::Replace($scene,'(?ms)^          m_propertyValues:.*?(?=^          m_keywordSelections:)',
 "          m_propertyValues: ~`n")
if($scene -notmatch 'm_name: Ground' -or $scene -notmatch 'm_modelGuid: 8c05d8c6-f8bb-8ebb-8b1d-90e2d3b66e45'){
 throw 'Authored plane identity/layout changed'
}
[IO.File]::WriteAllText("$fixture/Assets/Scenes/TR0_Native_Workload.creator",$scene)
'guid: 66666666-6666-4666-8666-666666666666'|Set-Content "$fixture/Assets/Scenes/TR0_Native_Workload.creator.meta" -Encoding utf8
"TR0_BASE0_FIXTURE_OK $fixture"
