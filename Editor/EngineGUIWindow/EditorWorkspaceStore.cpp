#include "EditorWorkspaceStore.h"
#include "EditorWindowNames.h"
#include "ViewportHostWindow.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>

static_assert(IMGUI_VERSION_NUM == 19280, "Revalidate workspace docking adapter for the new ImGui version");
namespace editor
{
    namespace
    {
        std::mutex mailboxMutex;
        workspace_action pendingAction{};
        std::string pendingPanel;
        workspace_status published;
        bool resetDialog{};
        std::filesystem::path override_path(const wchar_t* name, std::filesystem::path fallback)
        {
            wchar_t value[32768]{};
            const auto length=GetEnvironmentVariableW(name,value,32768);
            return length>0 && length<32768 ? std::filesystem::path(value) : fallback;
        }
        std::vector<workspace::alias> aliases(const window_table& windows)
        {
            std::vector<workspace::alias> result;
            for(const auto& a:EditorWindowName::legacy_names)
            {
                // Both pre-M4 bare names and M4's ### suffix hash are present in legacy files.
                for(const auto& old: {std::string(a.old_name), "###"+std::string(a.old_name)})
                    result.push_back({old,std::string(a.stable_id),ImHashStr(old.c_str()),ImHashStr(a.stable_id.data())});
            }
            // ImGui does not store the name Begin received. CreateNewWindowSettings skips
            // past "###" and keeps only the text after it, marker dropped, so a file the
            // editor itself wrote spells the window "Editor.Scene". Declaring that
            // spelling from the live table rather than from the legacy list is what keeps
            // a window added after W3 from reading back as an unknown section.
            for(const auto& entry:windows.entries)
            {
                const std::string id(entry.stable_id);
                if(!id.starts_with("###")) continue;
                const auto bare=id.substr(3);
                result.push_back({bare,id,ImHashStr(bare.c_str()),ImHashStr(id.c_str())});
            }
            return result;
        }
    }
    bool request_workspace_action(workspace_action action,std::string panel)
    {
        std::lock_guard lock(mailboxMutex);
        if(pendingAction!=workspace_action::none || published.pending) return false;
        pendingAction=action; pendingPanel=std::move(panel); published.pending=true; return true;
    }
    workspace_status get_workspace_status() { std::lock_guard lock(mailboxMutex); return published; }
    void draw_workspace_menu()
    {
        const auto status=get_workspace_status();
        ImGui::BeginDisabled(status.pending);
        if(ImGui::MenuItem("Save Workspace")) request_workspace_action(workspace_action::save);
        if(ImGui::MenuItem("Reload Workspace")) request_workspace_action(workspace_action::load);
        if(ImGui::MenuItem("Reset Layout...")) resetDialog=true;
        ImGui::EndDisabled();
        if(!status.error.empty()) ImGui::TextWrapped("Workspace: %s",status.error.c_str());
    }
    void draw_workspace_dialog()
    {
        if(resetDialog) { ImGui::OpenPopup("Reset Workspace"); resetDialog=false; }
        if(ImGui::BeginPopupModal("Reset Workspace",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Restore the default S&Box Compact layout?");
            ImGui::TextUnformatted("The current workspace will be backed up first.");
            if(ImGui::Button("Restore default"))
            { request_workspace_action(workspace_action::reset); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    EditorWorkspaceStore::EditorWorkspaceStore(window_table& windows):m_windows(windows)
    {
        m_path=(override_path(L"CREATOR_EDITOR_WORKSPACE_DIR",PathFinder::RuntimeDataPath("Editor/Workspaces"))/"active.workspace")
            .lexically_normal().make_preferred();
        m_legacy=override_path(L"CREATOR_EDITOR_LEGACY_INI",PathFinder::ConfigPath("imgui.ini")).lexically_normal().make_preferred();
        // The declaration table must already be filled. An empty one is not an empty
        // editor, it is a boot-order mistake, and the cost is silent: every restored
        // panel state is dropped and the declaration defaults win on every start.
        if(windows.entries.empty())
            throw std::runtime_error("register_editor_windows() must run before the workspace store reads the table");
        for(const auto& entry:windows.entries) if(entry.persist_open) m_defaults.emplace(entry.stable_id,entry.open);
        // Own the ini blob together with panel state; the backend must not write a second copy.
        ImGui::GetIO().IniFilename=nullptr;
        Load(true); Publish();
    }
    void EditorWorkspaceStore::Apply(const workspace::document& document,bool startup)
    {
        if(document.imgui_version!=IMGUI_VERSION_NUM || document.theme_version!=1)
            throw std::runtime_error("Workspace version requires migration");
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromMemory(document.ini.data(),document.ini.size());
        m_document=document;
        // W4: `viewport` 는 이제 창 이름이 아니라 Host 의 표시 모드 토큰이다.
        // 옛 파일이 든 값이 그대로 모드 이름이라 스키마를 올리지 않아도 읽힌다.
        windows::request_viewport_mode(document.viewport==EditorWindowName::kGame
            ? windows::viewport_mode::game : windows::viewport_mode::scene);
        m_focusViewport=EditorWindowName::kViewport;
        for(auto& entry:m_windows.entries)
        {
            if(!entry.persist_open) continue;
            const auto found=document.panels.find(std::string(entry.stable_id));
            entry.open=entry.role==window_role::central || (found==document.panels.end()?m_defaults.at(std::string(entry.stable_id)):found->second);
        }
        EditorSettingsStore::Get().Preferences().SetContentTreeWidth(document.tree_width);
        // Apply DPI once to floating windows. Dock layout scales with its root viewport.
        const float scale=ImGui::GetStyle().FontScaleDpi/std::max(0.5f,document.dpi);
        auto& g=*ImGui::GetCurrentContext();
        for(auto* setting=g.SettingsWindows.begin();setting;setting=g.SettingsWindows.next_chunk(setting))
        {
            if(setting->DockId || !std::string_view(setting->GetName()).starts_with("###Editor.")) continue;
            if(startup && scale!=1.f)
            {
                setting->Pos=ImVec2ih(static_cast<int>(setting->Pos.x*scale),static_cast<int>(setting->Pos.y*scale));
                setting->Size=ImVec2ih(static_cast<int>(setting->Size.x*scale),static_cast<int>(setting->Size.y*scale));
            }
        }
        m_buildDefault=false; m_saveRequested=true; m_writable=true;
    }
    void EditorWorkspaceStore::Load(bool startup)
    {
        try
        {
            m_status.error.clear(); m_status.recovered=false;
            if(std::filesystem::exists(m_path))
            {
                const auto bytes=workspace::read_file(m_path);
                Apply(workspace::decode(bytes),startup); m_lastBytes=bytes;
                m_status.message="Workspace restored";
            }
            else if(startup && std::filesystem::exists(m_legacy))
            {
                const auto bytes=workspace::read_file(m_legacy);
                workspace::backup(m_legacy,"pre-workspace-v1");
                auto legacy=m_document; legacy.ini=workspace::migrate_ini(bytes,aliases(m_windows));
                std::string error;
                if(!workspace::validate_ini(legacy.ini,error)) throw std::runtime_error(error);
                legacy.panels=m_defaults; legacy.dpi=ImGui::GetStyle().FontScaleDpi;
                legacy.tree_width=EditorSettingsStore::Get().Preferences().GetContentTreeWidth();
                Apply(legacy,startup); m_status.migrated=true; m_status.message="Legacy layout migrated; original preserved";
            }
            else
            {
                ImGui::ClearIniSettings(); m_buildDefault=true; m_saveRequested=true;
                for(auto& entry:m_windows.entries) if(entry.persist_open) entry.open=m_defaults.at(std::string(entry.stable_id));
                m_status.message="Default workspace";
            }
        }
        catch(const std::exception& ex)
        {
            m_status.error=ex.what(); m_status.recovered=true;
            // Preserve rejected bytes before allowing an automatic save to replace them.
            try { workspace::backup(m_path,"rejected"); }
            catch(const std::exception& backupError) { m_writable=false; m_status.error+="; "+std::string(backupError.what()); }
            ImGui::ClearIniSettings(); m_buildDefault=true; m_saveRequested=true;
            for(auto& entry:m_windows.entries) if(entry.persist_open) entry.open=m_defaults.at(std::string(entry.stable_id));
            // The reason belongs in the message, not only in `error`: `error` is the
            // last operation's outcome and the very next save clears it, which would
            // leave a recovered session with no record of what was wrong.
            m_status.message="Invalid layout preserved; restored default layout: "+m_status.error;
        }
        ++m_status.revision;
    }
    bool EditorWorkspaceStore::BeforeFrame()
    {
        workspace_action action; std::string panel;
        { std::lock_guard lock(mailboxMutex); action=pendingAction; panel=std::move(pendingPanel); pendingAction=workspace_action::none; }
        if(action!=workspace_action::none)
        {
            m_status.error.clear();
            try
            {
                if(action==workspace_action::load) Load(false);
                else if(action==workspace_action::reset)
                {
                    Save(); workspace::backup(m_path,"before-reset");
                    ImGui::ClearIniSettings(); m_buildDefault=true; m_saveRequested=true;
                    for(auto& entry:m_windows.entries) if(entry.persist_open) entry.open=m_defaults.at(std::string(entry.stable_id));
                    EditorSettingsStore::Get().Preferences().SetContentTreeWidth(220.f);
                    windows::request_viewport_mode(windows::viewport_mode::scene);
                    m_focusViewport=EditorWindowName::kViewport;
                    m_document.name="S&Box Compact"; m_status.message="Default layout restored; backup preserved";
                }
                else if(action==workspace_action::save) m_saveRequested=true;
                else
                {
                    auto* entry=find_window(m_windows,panel);
                    if(!entry || entry->role!=window_role::panel || !entry->closable) throw std::runtime_error("Panel is unavailable or cannot be closed");
                    entry->open=action==workspace_action::open_panel; m_saveRequested=true;
                }
            }
            catch(const std::exception& ex) { m_status.error=ex.what(); }
            ++m_status.revision;
        }
        m_status.pending=false; Publish();
        const bool build=m_buildDefault; m_buildDefault=false; return build;
    }
    void EditorWorkspaceStore::Save()
    {
        if(!m_writable) throw std::runtime_error("Workspace is read-only because its backup failed");
        auto value=m_document;
        std::size_t size{}; const auto* ini=ImGui::SaveIniSettingsToMemory(&size);
        // ImGui writes back the name Begin received, which is `label###id`. Store the
        // stable half only, so a renamed or re-iconed window keeps its dock slot and a
        // relabelled duplicate cannot appear as a second section (plan 1.4).
        value.ini=workspace::migrate_ini(std::string_view(ini,size),aliases(m_windows));
        std::string error; if(!workspace::validate_ini(value.ini,error)) {
            // Keep the rejected bytes next to the workspace. A validator message alone
            // does not say which section broke, and this path refuses to save at all,
            // so without the dump the layout that failed is gone when the editor exits.
            auto dump=m_path; dump+=".invalid"; try{ workspace::atomic_write(dump,value.ini); }catch(...){}
            throw std::runtime_error(error); }
        for(const auto& entry:m_windows.entries) if(entry.persist_open) value.panels[std::string(entry.stable_id)]=entry.open;
        value.dpi=ImGui::GetStyle().FontScaleDpi;
        value.width=ImGui::GetIO().DisplaySize.x; value.height=ImGui::GetIO().DisplaySize.y;
        if(value.width<=0.f || value.height<=0.f) return; // minimized windows never replace valid geometry
        value.tree_width=EditorSettingsStore::Get().Preferences().GetContentTreeWidth();
        value.imgui_version=IMGUI_VERSION_NUM;
        value.viewport=windows::viewport_mode::game==windows::get_viewport_mode()
            ?EditorWindowName::kGame:EditorWindowName::kScene;
        const auto bytes=workspace::encode(value);
        if(bytes!=m_lastBytes) { workspace::atomic_write(m_path,bytes); m_lastBytes=bytes; }
        m_document=std::move(value); m_status.ready=true; m_status.error.clear();
    }
    void EditorWorkspaceStore::EndFrame()
    {
        if(!m_focusViewport.empty())
        {
            if(auto* window=ImGui::FindWindowByName(m_focusViewport.c_str()))
            { ImGui::FocusWindow(window); m_focusViewport.clear(); }
        }
        const auto now=std::chrono::steady_clock::now();
        if(m_saveRequested || now-m_lastSave>=std::chrono::seconds(2))
        {
            try { Save(); }
            catch(const std::exception& ex) { m_status.error=ex.what(); }
            m_saveRequested=false; m_lastSave=now; ++m_status.revision;
        }
        Publish();
    }
    void EditorWorkspaceStore::SaveOnShutdown() noexcept
    {
        try { Save(); } catch(...) {} // Last valid file stays intact; shutdown must still release the host.
    }
    void EditorWorkspaceStore::Publish()
    {
        const auto path=m_path.u8string(); m_status.path.assign(path.begin(),path.end());
        m_status.panels.clear();
        for(const auto& entry:m_windows.entries) if(entry.persist_open) m_status.panels.emplace(entry.stable_id,entry.open);
        std::lock_guard lock(mailboxMutex);
        m_status.pending=m_saveRequested || pendingAction!=workspace_action::none;
        published=m_status;
    }
}
