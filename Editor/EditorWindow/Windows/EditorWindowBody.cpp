// 창 본문 보관소 (PHASE 21 M4 2·3단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.
// 여기에는 본문이 없고 ImGui도 부르지 않는다.

#include "EditorWindowBody.h"

#include <string_view>
#include <utility>
#include <vector>

namespace editor::windows
{
    namespace
    {
        // 스물다섯 개짜리 선형 탐색이다. 해시 테이블을 쓰지 않는 이유가 있다 —
        // 옛 `ImGuiRegister`의 `unordered_map`은 순회가 비결정적이라 골든을
        // 흔들었고(§1.3-4), 여기서 되풀이할 이유가 없다. 프레임당 스물다섯 번의
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

    namespace detail
    {
        void run_window_body(std::string_view stable_id)
        {
            if (std::function<void()>* body = find_body(stable_id))
            {
                if (*body)
                {
                    (*body)();
                }
            }
        }

        bool window_body_bound(std::string_view stable_id)
        {
            const std::function<void()>* body = find_body(stable_id);
            return nullptr != body && static_cast<bool>(*body);
        }
    }
}
