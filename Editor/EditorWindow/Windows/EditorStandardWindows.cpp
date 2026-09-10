// 표준 에디터 창 진입점 (PHASE 21 M4 2단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.
//
// 여기에는 본문이 없다. 창 클래스가 걸어 둔 본문을 이름으로 찾아 부르는
// 얇은 층뿐이고, ImGui를 부르지 않는다.

#include "EditorStandardWindows.h"

namespace editor::windows
{
    EDITOR_DEFINE_WINDOW_ENTRY(hierarchy,               EditorWindowName::kHierarchy)
    EDITOR_DEFINE_WINDOW_ENTRY(inspector,               EditorWindowName::kInspector)
    EDITOR_DEFINE_WINDOW_ENTRY(asset_bundle,            EditorWindowName::kAssetBundle)
    EDITOR_DEFINE_WINDOW_ENTRY(content_browser,         EditorWindowName::kContentBrowser)
    EDITOR_DEFINE_WINDOW_ENTRY(resource_counter,        EditorWindowName::kResourceCounter)

    EDITOR_DEFINE_WINDOW_ENTRY(render_pass,             EditorWindowName::kRenderPass)
    EDITOR_DEFINE_WINDOW_ENTRY(light_map,               EditorWindowName::kLightMap)
    EDITOR_DEFINE_WINDOW_ENTRY(collision_matrix,        EditorWindowName::kCollisionMatrix)
    EDITOR_DEFINE_WINDOW_ENTRY(texture_import_selector, EditorWindowName::kTextureImportSelector)
    EDITOR_DEFINE_WINDOW_ENTRY(material_picker,         EditorWindowName::kMaterialPicker)
}
