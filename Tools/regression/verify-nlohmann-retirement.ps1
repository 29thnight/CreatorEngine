param()

# SerializationPlan D4 — structured authoring backend를 ryml 하나로 고정한다.
# nlohmann은 header-only라 PE import로는 검출되지 않으므로 source, manifest,
# installed include/share/status를 모두 확인해야 실제 의존 제거를 증명할 수 있다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$failures = [Collections.Generic.List[string]]::new()
$sourceRoots = @('Engine', 'Editor', 'Player') | ForEach-Object {
    Join-Path $repoRoot $_
}
$sourceFiles = @($sourceRoots | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File -Include *.h,*.hpp,*.cpp,*.ixx
})
$sourceHits = @($sourceFiles | Select-String -Pattern `
    'nlohmann|#include\s*[<"]nlohmann/json\.hpp[>"]|using\s+json\s*=')
foreach ($hit in $sourceHits) {
    $failures.Add("source: $($hit.Path):$($hit.LineNumber): $($hit.Line.Trim())")
}

$manifest = Join-Path $repoRoot 'vcpkg.json'
if ((Get-Content -LiteralPath $manifest -Raw) -match 'nlohmann-json') {
    $failures.Add('manifest: vcpkg.json still declares nlohmann-json')
}

# ── installed 트리 검사 (2026-09-05 정정) ──
#
# 여기에 결함이 둘 있었다.
#
# 1. 실물 경로가 틀렸다. 매니페스트 모드의 설치 트리는
#    `vcpkg_installed\x64-windows\x64-windows\{include,share}`로 **한 단계 더**
#    중첩된다. 한 단계 얕은 경로를 보고 있었으므로 이 두 단정은 존재할 수 없는
#    경로를 검사하는 죽은 단정이었다 — 무엇이 설치돼도 초록이었다.
#
# 2. status 텍스트 단정이 유령을 잡고 있었다. `vcpkg_installed\vcpkg\status`에
#    nlohmann-json stanza가 둘 남아 있는데, x64-windows는
#    `purge ok not-installed`이고 x64-windows-idl0은 `install ok installed`다.
#    그런데 그 트리플렛 디렉터리는 존재하지 않고, 어떤 포트도 nlohmann-json을
#    Depends로 선언하지 않으며, vcpkg.json도 선언하지 않는다. 즉 과거 설치의
#    기록만 남은 것이다. status는 기록이고 **정본은 실물**이므로, 경로를 고쳐
#    실물을 재는 것으로 이 축을 대신한다. (로컬 트리를 재설치하면 그 기록도
#    사라지지만, 그것은 저장소가 아니라 각자 기계의 상태다 — vcpkg_installed는
#    .gitignore 대상이다.)
$installedRoot = Join-Path $repoRoot 'vcpkg_installed\x64-windows'
$installedTargets = @(
    (Join-Path $installedRoot 'include\nlohmann'),
    (Join-Path $installedRoot 'share\nlohmann-json'),
    (Join-Path $installedRoot 'x64-windows\include\nlohmann'),
    (Join-Path $installedRoot 'x64-windows\share\nlohmann-json')
)
foreach ($target in $installedTargets) {
    if (Test-Path -LiteralPath $target) {
        $failures.Add("installed tree remains: $target")
    }
}

$serializableHeader = Join-Path $repoRoot 'Engine\SceneRuntime\ISerializable.h'
if (Test-Path -LiteralPath $serializableHeader) {
    $failures.Add('retired ISerializable.h still exists')
}
$projectFiles = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Engine') `
    -Recurse -File -Include *.vcxproj,*.filters)
$projectHits = @($projectFiles | Select-String -Pattern 'ISerializable|nlohmann')
foreach ($hit in $projectHits) {
    $failures.Add("project: $($hit.Path):$($hit.LineNumber): $($hit.Line.Trim())")
}

$animatorText = (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\Animator.cpp') -Raw) +
    (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\Animator.h') -Raw)
if ($animatorText -match 'SerializeControllers|DeserializeControllers|ImportLegacyControllerJson|\.json') {
    $failures.Add('Animator legacy JSON entry point remains')
}

$inputText = (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\InputActionManager.cpp') -Raw) +
    (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\InputActionManager.h') -Raw)
if ($inputText -match 'SerializeMap|DeSerializeMap|\.json') {
    $failures.Add('InputMap legacy JSON contract remains')
}
$inputRoot = Join-Path $repoRoot 'Dynamic_CPP\Assets\InputMap'
$legacyInput = @(Get-ChildItem -LiteralPath $inputRoot -Filter '*.json' -File)
if ($legacyInput.Count -ne 0) {
    $failures.Add("InputMap JSON files remain: $($legacyInput.Count)")
}

$terrainText = (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\Terrain.cpp') -Raw) +
    (Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Editor\EngineEntry\EditorAssetDatabase.cpp') -Raw)
if ($terrainText -match 'LoadEditorSplatMap_Compat|json::|\bjson\s+[A-Za-z_]') {
    $failures.Add('Terrain legacy JSON/compat path remains')
}
if ($terrainText -notmatch 'schemaVersion') {
    $failures.Add('Terrain canonical YAML schema marker is missing')
}

"nlohmann retirement: sources=$($sourceFiles.Count) sourceHits=$($sourceHits.Count) projectHits=$($projectHits.Count) inputJson=$($legacyInput.Count)"
if ($failures.Count -gt 0) {
    'FAIL:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'nlohmann retirement: PASS (source=0 manifest=0 installed=0 ISerializable=0 animator-json=0 input-json=0 terrain-json=0)'
exit 0
