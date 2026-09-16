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
# PHASE 21 M4 2단계(d2b70402)부터 프레임은 창 선언이 열고, 이 파일은 본문만
# 묶는다. 경로는 세 고리다: 본문 묶기 → 선언의 panel → 선언이 부르는 진입점.
# 한 고리라도 빠지면 창은 목록에 없거나 빈 틀만 뜬다.
Assert-Matches $presentation 'bind_window_body\(\s*kMaterialPicker,\s*\[this\]\(\)\s*\{\s*RenderMaterialPicker\(\);'
Assert-Matches $presentation 'bind_window_body\(\s*kTextureImportSelector,\s*\[this\]\(\)\s*\{\s*RenderTextureImportSelector\(\);'
$windowDeclarations = "Editor\EditorWindow\Windows\EditorStandardWindows.h"
Assert-Matches $windowDeclarations 'panel<&windows::draw_material_picker>\(\s*EditorWindowName::kMaterialPicker,'
Assert-Matches $windowDeclarations 'panel<&windows::draw_texture_import_selector>\(\s*EditorWindowName::kTextureImportSelector,'
$windowEntries = "Editor\EditorWindow\Windows\EditorStandardWindows.cpp"
Assert-Matches $windowEntries 'EDITOR_DEFINE_WINDOW_ENTRY\(material_picker,\s*EditorWindowName::kMaterialPicker\)'
Assert-Matches $windowEntries 'EDITOR_DEFINE_WINDOW_ENTRY\(texture_import_selector,\s*EditorWindowName::kTextureImportSelector\)'
Assert-Matches $presentation 'ResolveFilePresentation'
# 폰트 파일 적재는 W1(afe52737)에서 EditorFontResources 한 자리로 모였다.
# 표시 계층은 그 창구로 small/extra_small 을 받는다.
Assert-Matches $presentation 'm_smallFont\s*=\s*::editor::fonts::add_optional_font\('
Assert-Matches $presentation 'm_extraSmallFont\s*=\s*::editor::fonts::add_optional_font\('
Assert-Matches $presentation 'CameraGizmo\.png'
Assert-Matches $presentation 'SetGizmoIconTextures\(m_gizmoIconTextures\)'

# ScriptBinder는 Editor singleton/DataSystem을 역참조하지 않고 명시적인 render
# 입력만 사용해야 한다. packet의 shared_ptr이 raw pass pointer의 수명을 붙든다.
Assert-DoesNotMatch "Engine\SceneRuntime\EnhancedGizmoSceneBinding.cpp" `
    '#include\s+"(?:DataSystem|EditorAssetPresentation)\.h"|DataSystems|EditorAssetPresentation::'
Assert-Matches "Engine\RenderEngine\EnhancedGizmoSceneBinding.h" `
    'std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures'
Assert-Matches "Engine\SceneRuntime\EnhancedGizmoSceneBinding.cpp" `
    'out\.iconTextures\s*=\s*std::move\(iconTextures\)'
Assert-Matches "Engine\RenderEngine\Render\Scene\EnhancedSceneRenderer.cpp" `
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
