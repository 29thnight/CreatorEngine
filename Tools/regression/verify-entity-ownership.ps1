# GCCE: static source-only ownership contract. This script does not build or run the engine.
# Runtime regression source covers lifecycle, DDOL, stale handles, cycles and fenced borrows.

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sceneHeader = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\Scene.h") -Raw -Encoding UTF8
$entityHeader = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\Entity.h") -Raw -Encoding UTF8
$sceneManagerHeader = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\SceneManager.h") -Raw -Encoding UTF8
$transferHeader = Get-Content (Join-Path $repoRoot "Engine\SceneRuntime\DetachedEntityTransfer.h") -Raw -Encoding UTF8

$failures = @()

foreach ($legacyPath in @(
    'Engine\SceneRuntime\GameObject.h',
    'Engine\SceneRuntime\GameObject.cpp',
    'Engine\SceneRuntime\GameObject.inl',
    'ScriptCore\GameObject.cs')) {
    if (Test-Path (Join-Path $repoRoot $legacyPath)) {
        $failures += "구 파일명이 다시 등장함: $legacyPath"
    }
}
foreach ($entityPath in @(
    'Engine\SceneRuntime\Entity.h',
    'Engine\SceneRuntime\Entity.cpp',
    'Engine\SceneRuntime\Entity.inl',
    'ScriptCore\Entity.cs')) {
    if (-not (Test-Path (Join-Path $repoRoot $entityPath))) {
        $failures += "Entity 파일이 없음: $entityPath"
    }
}

if ($sceneHeader -notmatch 'std::vector<gc::trace_ref<Entity>>\s+m_Entities') {
    $failures += "Scene::m_Entities must use traced GC graph edges"
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
if ($transferHeader -notmatch 'gc::root_ref<Entity>\s+entity') {
    $failures += "DDOL transfer must retain an owner-thread GC root"
}

if ($sceneHeader -notmatch 'class\s+\[\[reflgen::reflect\]\]\s+Scene\s*:\s*public\s+gc::managed') {
    $failures += 'Scene must be allocated as a GCCE managed object'
}
if ($entityHeader -notmatch 'std::vector<gc::trace_ref<Component>>\s+m_components') {
    $failures += 'Entity components must be traced GC graph edges'
}
if ($sceneManagerHeader -notmatch 'gc::domain\s+m_gcDomain' -or
    $sceneManagerHeader -notmatch 'std::vector<gc::root_ref<Scene>>\s+m_sceneRoots') {
    $failures += 'SceneManager must retain scenes in its single shared GC domain'
}
if ($sceneManagerHeader.IndexOf('gc::domain m_gcDomain') -gt $sceneManagerHeader.IndexOf('m_sceneRoots')) {
    $failures += 'SceneManager domain must be declared before roots so it is destroyed last'
}
if ($sceneHeader -notmatch 'static gc::root_ref<Scene> CreateNewScene\(gc::domain&' -or
    $sceneHeader -notmatch 'static gc::root_ref<Scene> LoadScene\(gc::domain&') {
    $failures += 'Scene factories must publish owner-thread roots in an explicit domain'
}
$sceneSource = Get-Content (Join-Path $repoRoot 'Engine\SceneRuntime\Scene.cpp') -Raw -Encoding UTF8
$sceneManagerSource = Get-Content (Join-Path $repoRoot 'Engine\SceneRuntime\SceneManager.cpp') -Raw -Encoding UTF8
if ($sceneSource -notmatch 'tracer\.visit\(m_Entities\)') {
    $failures += 'Scene gc_trace must visit the Entity graph independently of serialization'
}
if ($sceneManagerSource -notmatch 'm_gcDomain\.collect_step\(gc::step_budget') {
    $failures += 'Frame-boundary collection must use an explicit incremental soft budget'
}
if ($sceneSource -notmatch 'std::erase\(m_selectedEntities, released\.get\(\)\)' -or
    $sceneSource -notmatch 'std::erase\(m_simulationSelection, released\.get\(\)\)') {
    $failures += 'Slot release must prune all retained selection borrows'
}

$sourceRoots = @("Editor\EngineEntry", "Editor\EngineGUIWindow", "Engine\RenderEngine", "Engine\SceneRuntime") |
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

$projectText = Get-Content (Join-Path $repoRoot 'Engine\SceneRuntime\SceneRuntime.vcxproj') -Raw -Encoding UTF8
foreach ($entityFile in @('Entity.cpp', 'Entity.h', 'Entity.inl')) {
    if ($projectText -notmatch [regex]::Escape($entityFile)) {
        $failures += "SceneRuntime 프로젝트에 $entityFile 항목이 없음"
    }
}
if ($projectText -match 'GameObject\.(?:h|cpp|inl)') {
    $failures += 'SceneRuntime 프로젝트에 구 GameObject 파일 항목이 남아 있음'
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "실패: $_" }
    exit 1
}

"PASS — managed Scene/Entity graph, explicit scene/DDOL roots, owner-thread domain, budgeted collection, no shared_ptr<Entity>"
exit 0
