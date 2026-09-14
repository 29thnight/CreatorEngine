#pragma once
#include "EditorWorkspaceFile.h"
#include "EditorLayoutPreset.h"
#include "EditorWindowRegistry.h"
#include <chrono>

namespace editor
{
    enum class workspace_action { none, save, load, reset, open_panel, close_panel, apply_preset };
    struct workspace_status
    {
        bool ready{}, pending{}, recovered{}, migrated{};
        std::string path, message, error;
        // 지금 서 있는 배치의 신원(PHASE 21 W6). 계획서가 "active workspace 표시" 로
        // 적은 것이고, 메뉴·제목·CLI 가 같은 값을 읽는다.
        std::string preset, preset_label, name;
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
        std::string m_lastBytes;
        std::string m_focusViewport;
        std::chrono::steady_clock::time_point m_lastSave{};
    };
}
