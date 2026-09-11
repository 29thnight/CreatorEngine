// 메뉴 문맥 유도 (PHASE 21 M1).
//
// 이 TU 도 유니티에서 뺀다(Editor.vcxproj) — 선언 계층의 다른 구현 TU 와 같은 이유.

#include "EditorMenuTargets.h"

#include "EditorObjectOperations.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"

namespace editor::targets
{
    std::optional<entity_target> selected_entity()
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene) return std::nullopt;

        Entity* selected = scene->GetSelectedEntity();
        if (nullptr == selected) return std::nullopt;

        std::string identity = EditorObjectOperations::ObjectId(scene->HandleOf(selected->m_index));
        if (identity.empty()) return std::nullopt;

        return entity_target{ std::move(identity) };
    }
}
