#pragma once
#include "EditorWorkspaceFile.h"
#include "EditorLayoutPreset.h"
#include "EditorWindowRegistry.h"
#include <chrono>

namespace editor
{
    enum class workspace_action {
        none, save, load, reset, open_panel, close_panel, apply_preset,
        // PHASE 21 W6-2 — 이름 붙인 workspace 여럿.
        save_as,      ///< 지금 배치를 그 이름으로 굳힌다(있으면 덮되 먼저 백업)
        load_named,   ///< 그 이름의 배치를 활성으로 가져온다
        rename,       ///< **지금** 배치의 이름을 바꾼다(파일도 함께)
        delete_named  ///< 그 이름의 파일을 지운다(지우기 전에 백업)
    };
    struct workspace_status
    {
        bool ready{}, pending{}, recovered{}, migrated{};
        std::string path, message, error;
        // 지금 서 있는 배치의 신원(PHASE 21 W6). 계획서가 "active workspace 표시" 로
        // 적은 것이고, 메뉴·제목·CLI 가 같은 값을 읽는다.
        std::string preset, preset_label, name;
        /// 지금 이름이 **파일로 존재하는가.** preset 을 막 적용한 직후처럼 이름은
        /// 있는데 저장된 적 없는 상태가 있어서, 이름만으로는 Rename·Delete 가
        /// 가능한지 알 수 없다.
        bool named{};
        /// 이름 붙인 workspace 목록. 디렉터리를 프레임마다 훑지 않는다 —
        /// 바뀔 만한 일이 있을 때만 새로 읽는다(W7-1 이 브라우저에서 끊은 것과
        /// 같은 죄다).
        std::vector<std::string> workspaces;
        std::uint64_t revision{};
        std::map<std::string,bool> panels;
    };
    // Commands only enqueue work and read published values; ImGui belongs to the renderer.
    bool request_workspace_action(workspace_action action, std::string panel = {});
    workspace_status get_workspace_status();
    void draw_workspace_menu();
    void draw_workspace_dialog();

    class EditorWorkspaceStore
    {
    public:
        explicit EditorWorkspaceStore(window_table& windows);
        bool BeforeFrame(); // true requests the existing default dock builder
        void EndFrame();   // after all window End calls
        void SaveOnShutdown() noexcept;
        /// 지금 배치를 만드는 preset. 도크 빌더가 이것을 읽는다.
        const layout_preset& ActivePreset() const noexcept { return *m_preset; }
    private:
        void ApplyPreset(const layout_preset& preset);
        /// `<이름>.workspace`. 이름은 이미 `workspace::valid_name` 을 통과한 것이어야 한다.
        std::filesystem::path NamedPath(const std::string& name) const;
        void RefreshNames();
        void SaveAs(const std::string& name);
        void LoadNamed(const std::string& name);
        void Rename(const std::string& name);
        void DeleteNamed(const std::string& name);
        void Load(bool startup);
        void Apply(const workspace::document& document, bool startup);
        void Save();
        void Publish();
        window_table& m_windows;
        const layout_preset* m_preset{ &default_layout_preset() };
        std::filesystem::path m_path, m_legacy;
        std::map<std::string,bool> m_defaults;
        workspace::document m_document;
        workspace_status m_status;
        bool m_buildDefault{}, m_saveRequested{}, m_writable{true};
        std::vector<std::string> m_names;   ///< 디렉터리에서 읽은 목록. RefreshNames 만 채운다
        bool m_named{};                     ///< `m_document.name` 이 파일로 있는가
        std::string m_lastBytes;
        std::string m_focusViewport;
        std::chrono::steady_clock::time_point m_lastSave{};
    };
}
