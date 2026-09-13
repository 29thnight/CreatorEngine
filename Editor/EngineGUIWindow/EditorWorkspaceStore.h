#pragma once
#include "EditorWorkspaceFile.h"
#include "EditorWindowRegistry.h"
#include <chrono>

namespace editor
{
    enum class workspace_action { none, save, load, reset, open_panel, close_panel };
    struct workspace_status
    {
        bool ready{}, pending{}, recovered{}, migrated{};
        std::string path, message, error;
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
    private:
        void Load(bool startup);
        void Apply(const workspace::document& document, bool startup);
        void Save();
        void Publish();
        window_table& m_windows;
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
