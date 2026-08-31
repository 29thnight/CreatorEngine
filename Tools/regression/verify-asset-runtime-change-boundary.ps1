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
#
# ★ 범위를 case 본문으로 좁힌다. 원래 이 단정은 `ContentReload:[\s\S]*RetireCachedAsset`
# 이었는데, 그 `[\s\S]*`는 파일 끝까지 열려 있어 **Removed case나 함수 정의에 있는
# 등장**과도 맞는다 — ContentReload에서 호출을 지워도 통과한다. 아래 tempered
# 수량자는 첫 `break;`를 넘지 못하므로 항상 ContentReload 본문 안에서만 맞는다.
# (이 저장소에서 범위를 안 좁힌 정적 순서 단정이 이미 두 번 거짓 결과를 냈다.)
#
# 순서까지 못 박는 이유는 코드 주석에 적힌 계약 그대로다 — 먼저 이전 generation을
# cache lookup에서 분리한 뒤에 catalog를 갱신해야, 이 호출 이후의 load가 새 파일을
# 읽는다. 반대로 하면 그 사이의 load가 옛 캐시를 새 GUID로 집어 간다.
Assert-Matches "Engine\RenderEngine\DataSystem.cpp" `
    'case RuntimeAssetChangeKind::ContentReload:(?:(?!break;)[\s\S])*?RetireCachedAsset\(assetType, change\.path, change\.guid, false\)(?:(?!break;)[\s\S])*?RegisterAssetMeta\(\*m_assetMetaRegistry, change\.guid, change\.path\)'
# 분리한 generation을 **실제로 붙든다**. 이름만 확인하면 `clear()`로 바뀌어도
# 통과하므로 `emplace_back(std::move(generation))`까지 본다.
Assert-Matches "Engine\RenderEngine\DataSystem.cpp" `
    'm_retiredTextureGenerations\.emplace_back\(std::move\(generation\)\)'

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
