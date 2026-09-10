// 표준 에디터 창 진입점 (PHASE 21 M4 2단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.
//
// 여기에는 본문이 없다. 창 클래스가 걸어 둔 본문을 이름으로 찾아 부르는
// 얇은 층뿐이고, ImGui를 부르지 않는다.

#include "EditorStandardWindows.h"

#include <string_view>
#include <utility>
#include <vector>

namespace editor::windows
{
    namespace
    {
        // 열 개짜리 선형 탐색이다. 해시 테이블을 쓰지 않는 이유가 있다 —
        // 옛 `ImGuiRegister`의 `unordered_map`은 순회가 비결정적이라 골든을
        // 흔들었고(§1.3-4), 여기서 되풀이할 이유가 없다. 프레임당 열 번의
        // 문자열 비교는 이 창들이 그리는 일에 비하면 없는 값이다.
        using binding = std::pair<std::string_view, std::function<void()>>;

        std::vector<binding>& bindings()
        {
            static std::vector<binding> store;
            return store;
        }

        std::function<void()>* find_body(std::string_view stable_id)
        {
            for (binding& one : bindings())
            {
                if (one.first == stable_id)
                {
                    return &one.second;
                }
            }
            return nullptr;
        }

        void run(std::string_view stable_id)
        {
            if (std::function<void()>* body = find_body(stable_id))
            {
                if (*body)
                {
                    (*body)();
                }
            }
        }

        bool bound(std::string_view stable_id)
        {
            const std::function<void()>* body = find_body(stable_id);
            return nullptr != body && static_cast<bool>(*body);
        }
    }

    void bind_window_body(std::string_view stable_id, std::function<void()> body)
    {
        if (std::function<void()>* existing = find_body(stable_id))
        {
            *existing = std::move(body);
            return;
        }
        bindings().emplace_back(stable_id, std::move(body));
    }

    void unbind_window_body(std::string_view stable_id)
    {
        for (auto it = bindings().begin(); it != bindings().end(); ++it)
        {
            if (it->first == stable_id)
            {
                bindings().erase(it);
                return;
            }
        }
    }

    void draw_hierarchy()               { run(EditorWindowName::kHierarchy); }
    bool has_hierarchy()                { return bound(EditorWindowName::kHierarchy); }

    void draw_inspector()               { run(EditorWindowName::kInspector); }
    bool has_inspector()                { return bound(EditorWindowName::kInspector); }

    void draw_asset_bundle()            { run(EditorWindowName::kAssetBundle); }
    bool has_asset_bundle()             { return bound(EditorWindowName::kAssetBundle); }

    void draw_content_browser()         { run(EditorWindowName::kContentBrowser); }
    bool has_content_browser()          { return bound(EditorWindowName::kContentBrowser); }

    void draw_resource_counter()        { run(EditorWindowName::kResourceCounter); }
    bool has_resource_counter()         { return bound(EditorWindowName::kResourceCounter); }

    void draw_render_pass()             { run(EditorWindowName::kRenderPass); }
    bool has_render_pass()              { return bound(EditorWindowName::kRenderPass); }

    void draw_light_map()               { run(EditorWindowName::kLightMap); }
    bool has_light_map()                { return bound(EditorWindowName::kLightMap); }

    void draw_collision_matrix()        { run(EditorWindowName::kCollisionMatrix); }
    bool has_collision_matrix()         { return bound(EditorWindowName::kCollisionMatrix); }

    void draw_texture_import_selector() { run(EditorWindowName::kTextureImportSelector); }
    bool has_texture_import_selector()  { return bound(EditorWindowName::kTextureImportSelector); }

    void draw_material_picker()         { run(EditorWindowName::kMaterialPicker); }
    bool has_material_picker()          { return bound(EditorWindowName::kMaterialPicker); }
}
