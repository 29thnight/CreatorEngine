// 에디터 기본 메뉴 동작 본문 (PHASE 21 M1).
//
// 이 TU 도 유니티에서 뺀다(Editor.vcxproj) — 선언 계층의 다른 구현 TU 와 같은
// 이유로, 각 TU 가 자기 include 를 소유하는지 평상시 빌드가 검사하게 둔다.
//
// ── 스레드 규약 (A.6) ─────────────────────────────────────────────────────
//
// 메뉴 콜백은 PresentationThread 에서 `m_sceneStructureMutex` 아래 돈다. 그래서
// 씬을 직접 만지는 동작은 `ConsoleCommandSystem::EnqueueStructured` 로 게임
// 스레드에 넘긴다. **그때 completion 을 반드시 넘긴다** — 넘기지 않으면 배치
// 큐로 가서 `CommandSession::Batch()` 에 누적되고 에디터 프로세스의 종료 코드를
// 오염시킨다(LC5 가 HTTP 에서 고친 그 결함).
//
// 여기 실린 동작 중 씬을 만지는 것은 없다. 신원 복사는 이미 값으로 받은 문자열을
// 클립보드에 넣을 뿐이고, 진단 둘은 명령으로 넘기며, 파일 삭제는 씬이 아니라
// 파일 시스템이다(인라인 구현도 같은 스레드에서 같은 일을 하고 있었다).

#include "EditorCoreMenus.h"

#include "ConsoleCommandSystem.h"
#include "ImGui.h"
#include "Core.Minimal.h"

#include <string>
#include <vector>

namespace editor::menus
{
    namespace
    {
        void put_on_clipboard(const std::string& text, const char* what)
        {
            if (text.empty())
            {
                Debug->LogWarning(std::string("[메뉴] 복사할 ") + what + " 이 비어 있다");
                return;
            }
            ImGui::SetClipboardText(text.c_str());
            Debug->Log(std::string("[메뉴] 클립보드에 ") + what + ": " + text);
        }

        /// 명령 하나를 게임 스레드로 넘긴다. completion 은 GT 에서 불리므로 짧다.
        void run_command(std::string name)
        {
            std::vector<std::string> arguments{ name };
            const bool accepted = ConsoleCommandSystem::Get().EnqueueStructured(
                std::move(arguments),
                [name](const CommandCore::CommandResult& result,
                       const ConsoleCommandSystem::CommandTiming&)
                {
                    // GT 에서 불린다 — 값만 찍고 곧 반환한다.
                    if (result.IsSuccess())
                    {
                        Debug->Log("[메뉴] " + name + " 완료");
                    }
                    else
                    {
                        Debug->LogError("[메뉴] " + name + " 실패: " + result.message);
                    }
                });

            if (!accepted)
            {
                Debug->LogError("[메뉴] " + name + " 적재 거부 — 서비스 큐가 가득하다");
            }
        }
    }

    void copy_entity_identity(const entity_target& target)
    {
        put_on_clipboard(target.identity, "엔티티 신원");
    }

    void copy_asset_path(const asset_target& target)
    {
        put_on_clipboard(target.path.string(), "자산 경로");
    }

    void copy_folder_path(const folder_target& target)
    {
        put_on_clipboard(target.path.string(), "폴더 경로");
    }

    void copy_component_identity(const component_target& target)
    {
        // CLI 가 컴포넌트를 가리킬 때 쓰는 순서대로 — 엔티티 신원 다음 타입 이름.
        put_on_clipboard(target.entity_identity + " " + target.component, "컴포넌트 신원");
    }

    bool has_meta_sidecar(const asset_target& target)
    {
        if (target.path.empty()) return false;
        std::error_code error;
        const file::path meta = file::path(target.path).concat(".meta");
        return file::exists(meta, error);
    }

    void copy_meta_path(const asset_target& target)
    {
        put_on_clipboard(file::path(target.path).concat(".meta").string(), ".meta 경로");
    }

    void report_declared_windows()
    {
        run_command("editor.windows");
    }

    void run_declaration_selftest()
    {
        run_command("editor.selftest");
    }

    void delete_asset(const asset_target& target)
    {
        // 인라인 두 벌이 하던 일을 그대로 한다 — 파일 하나를 지우고, `.cpp` 면
        // 짝 `.h` 도 같이 지운다. 달라진 것은 **확인을 거친다**는 것뿐이고(선언의
        // `confirm`), 복제가 하나로 줄었다는 것이다.
        if (target.path.empty())
        {
            Debug->LogWarning("[메뉴] 지울 대상이 비어 있다");
            return;
        }

        std::error_code error;
        if (!file::remove(target.path, error) || error)
        {
            Debug->LogError("[메뉴] 삭제 실패: " + target.path.string() +
                            " (" + error.message() + ")");
            return;
        }
        Debug->Log("[메뉴] 삭제: " + target.path.string());

        if (".cpp" != target.path.extension()) return;

        file::path header = target.path;
        header.replace_extension(".h");
        if (!file::exists(header, error)) return;

        if (!file::remove(header, error) || error)
        {
            Debug->LogError("[메뉴] 짝 헤더 삭제 실패: " + header.string());
            return;
        }
        Debug->Log("[메뉴] 삭제: " + header.string());
    }
}
