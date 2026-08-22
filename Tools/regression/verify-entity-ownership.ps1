# E5-d: Entity 소유권이 다시 shared_ptr로 되돌아가지 않는지 정적으로 고정한다.
# 런타임 게이트가 파괴/DDOL 이송을 담당하고, 이 검사는 저장 타입과 공개 소스의
# shared ownership 재유입을 잡는다.

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sceneHeader = Get-Content (Join-Path $repoRoot "ScriptBinder\Scene.h") -Raw -Encoding UTF8
$entityHeader = Get-Content (Join-Path $repoRoot "ScriptBinder\Entity.h") -Raw -Encoding UTF8
$sceneManagerHeader = Get-Content (Join-Path $repoRoot "ScriptBinder\SceneManager.h") -Raw -Encoding UTF8
$transferHeader = Get-Content (Join-Path $repoRoot "ScriptBinder\DetachedEntityTransfer.h") -Raw -Encoding UTF8

$failures = @()

foreach ($legacyPath in @(
    'ScriptBinder\GameObject.h',
    'ScriptBinder\GameObject.cpp',
    'ScriptBinder\GameObject.inl',
    'ScriptCore\GameObject.cs')) {
    if (Test-Path (Join-Path $repoRoot $legacyPath)) {
        $failures += "구 파일명이 다시 등장함: $legacyPath"
    }
}
foreach ($entityPath in @(
    'ScriptBinder\Entity.h',
    'ScriptBinder\Entity.cpp',
    'ScriptBinder\Entity.inl',
    'ScriptCore\Entity.cs')) {
    if (-not (Test-Path (Join-Path $repoRoot $entityPath))) {
        $failures += "Entity 파일이 없음: $entityPath"
    }
}

if ($sceneHeader -notmatch 'std::vector<std::unique_ptr<Entity>>\s+m_Entities') {
    $failures += "Scene::m_Entities가 unique_ptr 슬롯 저장소가 아니다"
}
if ($entityHeader -match 'enable_shared_from_this\s*<\s*Entity\s*>') {
    $failures += "Entity가 enable_shared_from_this를 다시 상속한다"
}
if ($sceneManagerHeader -notmatch 'std::vector<Object\*>\s+m_dontDestroyOnLoadObjects') {
    $failures += "DDOL 상시 목록이 비소유 Object* 목록이 아니다"
}
if ($sceneManagerHeader -notmatch 'std::vector<DetachedEntityTransfer>\s+m_detachedDontDestroyOnLoadObjects') {
    $failures += "DDOL 이송 중 저장소가 계층 transfer 레코드 벡터가 아니다"
}
if ($transferHeader -notmatch 'std::unique_ptr<Entity>\s+entity') {
    $failures += "DDOL 계층 transfer 레코드가 Entity 단독 소유권을 보존하지 않는다"
}

$sourceRoots = @("EngineEntry", "EngineGUIWindow", "RenderEngine", "ScriptBinder") |
    ForEach-Object { Join-Path $repoRoot $_ }
$sourceFiles = Get-ChildItem $sourceRoots -Recurse -File -Include *.h,*.hpp,*.cpp,*.inl |
    Where-Object { $_.FullName -notmatch '\\x64\\|\\Generated\\' }
$sharedEntityRefs = $sourceFiles | Select-String -Pattern 'std::shared_ptr\s*<\s*Entity\s*>'
if ($sharedEntityRefs) {
    $locations = $sharedEntityRefs |
        ForEach-Object { "$($_.Path):$($_.LineNumber)" } |
        Sort-Object -Unique
    $failures += "std::shared_ptr<Entity> 재유입: $($locations -join ', ')"
}

$legacyIncludes = $sourceFiles | Select-String -SimpleMatch '#include "GameObject.h"'
if ($legacyIncludes) {
    $locations = $legacyIncludes |
        ForEach-Object { "$($_.Path):$($_.LineNumber)" } |
        Sort-Object -Unique
    $failures += "GameObject.h include 재유입: $($locations -join ', ')"
}

$projectText = Get-Content (Join-Path $repoRoot 'ScriptBinder\ScriptBinder.vcxproj') -Raw -Encoding UTF8
foreach ($entityFile in @('Entity.cpp', 'Entity.h', 'Entity.inl')) {
    if ($projectText -notmatch [regex]::Escape($entityFile)) {
        $failures += "ScriptBinder 프로젝트에 $entityFile 항목이 없음"
    }
}
if ($projectText -match 'GameObject\.(?:h|cpp|inl)') {
    $failures += 'ScriptBinder 프로젝트에 구 GameObject 파일 항목이 남아 있음'
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "실패: $_" }
    exit 1
}

"전체 통과 — Entity 파일명/프로젝트/include 수렴, Scene·DDOL 단독 소유, shared_ptr<Entity> 0건"
exit 0
