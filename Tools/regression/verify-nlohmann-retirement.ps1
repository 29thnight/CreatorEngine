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

$installedRoot = Join-Path $repoRoot 'vcpkg_installed\x64-windows'
$installedTargets = @(
    (Join-Path $installedRoot 'include\nlohmann'),
    (Join-Path $installedRoot 'share\nlohmann-json')
)
foreach ($target in $installedTargets) {
    if (Test-Path -LiteralPath $target) {
        $failures.Add("installed tree remains: $target")
    }
}
$statusFile = Join-Path $repoRoot 'vcpkg_installed\vcpkg\status'
if ((Test-Path -LiteralPath $statusFile) -and
    (Get-Content -LiteralPath $statusFile -Raw) -match '(?m)^Package:\s*nlohmann-json\s*$') {
    $failures.Add('installed status still registers nlohmann-json')
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
