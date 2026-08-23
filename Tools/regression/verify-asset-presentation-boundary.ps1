$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function Read-Source([string]$relativePath) {
    Get-Content -LiteralPath (Join-Path $repoRoot $relativePath) -Raw
}

function Assert-Matches([string]$relativePath, [string]$pattern) {
    if ((Read-Source $relativePath) -notmatch $pattern) {
        throw "required asset-presentation route is missing: $relativePath / $pattern"
    }
}

function Assert-DoesNotMatch([string]$relativePath, [string]$pattern) {
    if ((Read-Source $relativePath) -match $pattern) {
        throw "runtime source owns forbidden Editor presentation state: $relativePath / $pattern"
    }
}

# AssetRuntime은 cache/catalog만 소유한다. ImGui context, picker transfer,
# browser/gizmo icon, font가 되돌아오면 E2 경계가 다시 합쳐진 것이다.
foreach ($runtimeFile in @("Engine\RenderEngine\DataSystem.h", "Engine\RenderEngine\DataSystem.cpp")) {
    Assert-DoesNotMatch $runtimeFile `
        'ImGui|RenderForEditer|SelectMat(?:e|a)rial|FileTypeIcon|kExtensionMap|(?:Small|small|extraSmall)Font|(?:Unknown|Texture|Model|Assets|Folder|Shader|Code|Camera|MainLight|PointLight|SpotLight|DirectionalLight)Icon|m_trasfarMaterial'
}

$presentation = "Editor\EngineEntry\EditorAssetPresentation.cpp"
Assert-Matches $presentation 'ContextRegister\(kMaterialPicker'
Assert-Matches $presentation 'ContextRegister\(kTextureImportSelector'
Assert-Matches $presentation 'ResolveFilePresentation'
Assert-Matches $presentation 'AddFontFromFileTTF'
Assert-Matches $presentation 'CameraGizmo\.png'
Assert-Matches $presentation 'SetGizmoIconTextures\(m_gizmoIconTextures\)'

# ScriptBinder는 Editor singleton/DataSystem을 역참조하지 않고 명시적인 render
# 입력만 사용해야 한다. packet의 shared_ptr이 raw pass pointer의 수명을 붙든다.
Assert-DoesNotMatch "Engine\ScriptBinder\EnhancedGizmoSceneBinding.cpp" `
    '#include\s+"(?:DataSystem|EditorAssetPresentation)\.h"|DataSystems|EditorAssetPresentation::'
Assert-Matches "Engine\RenderEngine\EnhancedGizmoSceneBinding.h" `
    'std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures'
Assert-Matches "Engine\ScriptBinder\EnhancedGizmoSceneBinding.cpp" `
    'out\.iconTextures\s*=\s*std::move\(iconTextures\)'
Assert-Matches "Engine\RenderEngine\Render\Scene\EnhancedSceneRendererLive.cpp" `
    'CaptureEnhancedGizmoSceneData\(view\.camera, collectColliders,\s*gizmoIconTextures'

Assert-Matches "Editor\EngineGUIWindow\ContentsBrowserWindow.cpp" `
    'EditorAssetPresentation::Get\(\)\.ResolveFilePresentation'
Assert-Matches "Editor\EngineGUIWindow\ImGuiDrawHelperMeshRenderer.cpp" `
    'EditorAssetPresentation::Get\(\)\.TakeSelectedMaterial'

$playerFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot "Player") `
    -Recurse -File -Include *.cpp,*.h
$playerText = ($playerFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw
}) -join "`n"
if ($playerText -match '#include\s+"EditorAssetPresentation\.h"|EditorAssetPresentation::Get\(\)\.(?:Initialize|Open|Queue)') {
    throw "Player installs or opens Editor asset presentation"
}

"asset presentation boundary: PASS"
