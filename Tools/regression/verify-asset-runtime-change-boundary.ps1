$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function Read-Source([string]$relativePath) {
    Get-Content -LiteralPath (Join-Path $repoRoot $relativePath) -Raw
}

function Assert-Matches([string]$relativePath, [string]$pattern) {
    if ((Read-Source $relativePath) -notmatch $pattern) {
        throw "required runtime asset-change route is missing: $relativePath / $pattern"
    }
}

function Assert-DoesNotMatch([string]$relativePath, [string]$pattern) {
    if ((Read-Source $relativePath) -match $pattern) {
        throw "forbidden asset boundary route is present: $relativePath / $pattern"
    }
}

# Core는 Editor의 import 구현을 알지 않고 게시 완료 결과만 받는다.
Assert-Matches "Engine\RenderEngine\DataSystem.h" `
    'enum class RuntimeAssetChangeKind[\s\S]*CatalogUpsert[\s\S]*ContentReload[\s\S]*Removed'
Assert-Matches "Engine\RenderEngine\DataSystem.h" `
    'void ApplyAssetChange\(const RuntimeAssetChange& change\)'
Assert-DoesNotMatch "Engine\RenderEngine\DataSystem.h" `
    'RegisterFileGuid\(|UnregisterFilePath\('

# reload는 cache key를 버리면서 이전 raw-reference generation의 수명을 보존한다.
Assert-Matches "Engine\RenderEngine\DataSystem.cpp" `
    'case RuntimeAssetChangeKind::ContentReload:[\s\S]*RetireCachedAsset\(assetType, change\.path\)'
Assert-Matches "Engine\RenderEngine\DataSystem.cpp" `
    'm_retiredAssetGenerations\.emplace_back'

# Editor는 source/meta 게시 뒤에만 reload/upsert/remove를 발행한다.
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'const FileGuid guid = CreateMetaLocked\(destination\);[\s\S]*RuntimeAssetChangeKind::ContentReload'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'RuntimeAssetChangeKind::CatalogUpsert'
Assert-Matches "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'RuntimeAssetChangeKind::Removed'
Assert-DoesNotMatch "Editor\EngineEntry\EditorAssetDatabase.cpp" `
    'RegisterFileGuid\(|UnregisterFilePath\('
Assert-DoesNotMatch "Engine\SceneRuntime\PrefabUtility.cpp" `
    'RegisterFileGuid\('

# Player는 source change 생산자를 설치하거나 직접 발행하지 않는다.
$playerText = (Get-ChildItem -LiteralPath (Join-Path $repoRoot "Player") `
    -Recurse -File -Include *.cpp,*.h | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw
    }) -join "`n"
if ($playerText -match 'ApplyAssetChange|RuntimeAssetChangeKind::') {
    throw "Player publishes Editor/runtime asset changes"
}

"runtime asset-change boundary: PASS"
