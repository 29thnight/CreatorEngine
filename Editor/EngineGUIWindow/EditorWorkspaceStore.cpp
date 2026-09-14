#include "EditorWorkspaceStore.h"
#include "EditorWindowNames.h"
#include "ViewportHostWindow.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include "EditorTheme.h"
#include <algorithm>
#include <cstring>
#include <system_error>
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
        // 이름을 받는 대화 상자 셋. `nameBuffer` 를 셋이 함께 쓰고, 여는 쪽이
        // 채워 넣는다 — 동시에 둘이 열리지 않으므로 버퍼를 나눌 이유가 없다.
        enum class name_dialog { none, save_as, rename };
        name_dialog nameDialog{ name_dialog::none };
        char nameBuffer[128]{};
        std::string deleteTarget;
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
        // 지금 무엇이 서 있는지 먼저 보인다. 계획서 W6 의 "active workspace 표시" 다 —
        // preset 을 고르는 자리와 지금 고른 것이 떨어져 있으면 무엇을 되돌리는지 모른다.
        ImGui::TextDisabled("Layout: %s",status.preset_label.empty()?"(none)":status.preset_label.c_str());
        ImGui::Separator();
        ImGui::BeginDisabled(status.pending);
        if(ImGui::BeginMenu("Layout Preset"))
        {
            for(const auto& preset:layout_presets())
            {
                const std::string label(preset.label);
                if(ImGui::MenuItem(label.c_str(),nullptr,status.preset==preset.id))
                    request_workspace_action(workspace_action::apply_preset,std::string(preset.id));
            }
            ImGui::EndMenu();
        }
        // 이름 붙인 배치 여럿(PHASE 21 W6-2). preset 은 **출발점**이고 이쪽은
        // 사람이 만든 것이라, 같은 메뉴라도 층을 나눈다.
        if(ImGui::BeginMenu("Workspaces",!status.workspaces.empty()))
        {
            for(const auto& name:status.workspaces)
                if(ImGui::MenuItem(name.c_str(),nullptr,status.named && name==status.name))
                    request_workspace_action(workspace_action::load_named,name);
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Save Workspace As..."))
        {
            const auto source=status.name.substr(0,sizeof(nameBuffer)-1);
            std::memcpy(nameBuffer,source.c_str(),source.size()+1);
            nameDialog=name_dialog::save_as;
        }
        // 이름이 파일로 서 있지 않으면 바꿀 것도 지울 것도 없다. 눌리지 않게
        // 두는 편이 눌렀다가 "이름이 없다" 를 읽는 것보다 정직하다.
        ImGui::BeginDisabled(!status.named);
        if(ImGui::MenuItem("Rename Workspace..."))
        {
            const auto source=status.name.substr(0,sizeof(nameBuffer)-1);
            std::memcpy(nameBuffer,source.c_str(),source.size()+1);
            nameDialog=name_dialog::rename;
        }
        if(ImGui::MenuItem("Delete Workspace...")) deleteTarget=status.name;
        ImGui::EndDisabled();
        ImGui::Separator();
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
            const auto status=get_workspace_status();
            ImGui::Text("Restore the default %s layout?",
                status.preset_label.empty()?"S&Box Compact":status.preset_label.c_str());
            ImGui::TextUnformatted("The current workspace will be backed up first.");
            if(ImGui::Button("Restore default"))
            { request_workspace_action(workspace_action::reset); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(nameDialog!=name_dialog::none) { ImGui::OpenPopup("Workspace Name"); }
        if(ImGui::BeginPopupModal("Workspace Name",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool renaming=nameDialog==name_dialog::rename;
            ImGui::TextUnformatted(renaming?"Rename the current workspace:":"Save the current layout as:");
            ImGui::SetNextItemWidth(ThemePixels(280.f));
            ImGui::InputText("##workspace-name",nameBuffer,sizeof(nameBuffer));
            // 만들기 전에 거절한다. 파일 이름으로 못 쓰는 이름을 받아 두고
            // 저장할 때 실패하면, 사람은 무엇이 문제인지 모른 채 배치를 잃는다.
            std::string error;
            const bool ok=workspace::valid_name(nameBuffer,error);
            if(!ok) ImGui::TextWrapped("%s",error.c_str());
            ImGui::BeginDisabled(!ok);
            if(ImGui::Button(renaming?"Rename":"Save"))
            {
                request_workspace_action(renaming?workspace_action::rename:workspace_action::save_as,nameBuffer);
                nameDialog=name_dialog::none; ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) { nameDialog=name_dialog::none; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if(!deleteTarget.empty()) ImGui::OpenPopup("Delete Workspace");
        if(ImGui::BeginPopupModal("Delete Workspace",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete the workspace \"%s\"?",deleteTarget.c_str());
            ImGui::TextUnformatted("A backup is kept next to it; the layout on screen stays.");
            if(ImGui::Button("Delete"))
            {
                request_workspace_action(workspace_action::delete_named,deleteTarget);
                deleteTarget.clear(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) { deleteTarget.clear(); ImGui::CloseCurrentPopup(); }
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
        Load(true); RefreshNames(); Publish();
    }
    void EditorWorkspaceStore::Apply(const workspace::document& document,bool startup)
    {
        if(document.imgui_version!=IMGUI_VERSION_NUM || document.theme_version!=1)
            throw std::runtime_error("Workspace version requires migration");
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromMemory(document.ini.data(),document.ini.size());
        m_document=document;
        // 파일이 든 preset 이 없는 이름이면(손으로 고쳤거나 옛 판) 기본으로 돌린다.
        // 배치 자체는 ini 가 들고 있으므로 이름 하나 때문에 배치를 버리지 않는다.
        if(const layout_preset* named=find_layout_preset(document.preset)) m_preset=named;
        else { m_preset=&default_layout_preset(); m_document.preset=std::string(m_preset->id); }
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
    void EditorWorkspaceStore::ApplyPreset(const layout_preset& preset)
    {
        // 사람이 만진 배치를 덮어쓰지 않는다 — 계획서 W6 의 판정 뒷절이다.
        // 먼저 지금 것을 파일로 굳히고, 그 파일을 백업한 뒤에야 새 배치를 세운다.
        // 순서가 반대면 백업에 **이전 실행의 배치**가 담긴다.
        Save();
        workspace::backup(m_path,"before-preset");
        m_preset=&preset;
        ImGui::ClearIniSettings();
        m_buildDefault=true; m_saveRequested=true;
        for(auto& entry:m_windows.entries)
        {
            if(!entry.persist_open) continue;
            switch(visibility_of(entry,preset))
            {
            case preset_visibility::opened: entry.open=true; break;
            case preset_visibility::closed: entry.open=false; break;
            // 지정이 없으면 **사람이 둔 대로** 둔다. preset 은 자리를 말하는 것이지
            // 열려 있던 창을 임의로 닫는 것이 아니다.
            case preset_visibility::inherit: break;
            }
        }
        m_document.preset=std::string(preset.id);
        m_document.name=std::string(preset.label);
        m_status.message="Layout preset applied: "+std::string(preset.label)+"; backup preserved";
    }
    std::filesystem::path EditorWorkspaceStore::NamedPath(const std::string& name) const
    {
        // `std::filesystem::path` 를 좁은 문자열로 만들면 Windows 의 ANSI 코드
        // 페이지로 읽는다. 한글 이름이 그 자리에서 깨지므로 u8 로 건넨다.
        const auto file=name+".workspace";
        const std::u8string wide(file.begin(),file.end());
        return (m_path.parent_path()/std::filesystem::path(wide)).lexically_normal().make_preferred();
    }
    void EditorWorkspaceStore::RefreshNames()
    {
        m_names.clear();
        std::error_code ec;
        std::filesystem::directory_iterator it(m_path.parent_path(),ec);
        if(!ec)
        {
            for(const auto& entry:it)
            {
                if(!entry.is_regular_file(ec)||ec) { ec.clear(); continue; }
                const auto& file=entry.path();
                // 백업은 `active.workspace.before-reset` 처럼 확장자가 이유 쪽이라
                // 여기서 저절로 빠진다. `.invalid` 덤프도 마찬가지다.
                if(file.extension()!=".workspace") continue;
                const auto stem=file.stem().u8string();
                std::string name(stem.begin(),stem.end());
                if(name=="active") continue;
                m_names.push_back(std::move(name));
            }
        }
        std::sort(m_names.begin(),m_names.end());
        m_named=!m_document.name.empty() &&
            std::find(m_names.begin(),m_names.end(),m_document.name)!=m_names.end();
    }
    void EditorWorkspaceStore::SaveAs(const std::string& name)
    {
        std::string error;
        if(!workspace::valid_name(name,error)) throw std::runtime_error(error);
        // 이름을 문서에 **먼저** 적고 굳힌다. 활성 파일과 이름 붙인 파일이 같은
        // 바이트여야 다음 기동이 "지금 어느 배치를 쓰는 중인가" 를 그대로 읽는다.
        const auto previous=m_document.name;
        m_document.name=name;
        try { Save(); }
        catch(...) { m_document.name=previous; throw; }
        const auto target=NamedPath(name);
        // 같은 이름으로 다시 저장하는 것은 흔한 일이다. 그때 이전 배치가 조용히
        // 사라지면 되돌릴 것이 없으므로 덮기 전에 남긴다.
        if(std::filesystem::exists(target)) workspace::backup(target,"before-overwrite");
        workspace::atomic_write(target,workspace::encode(m_document));
        RefreshNames();
        m_status.message="Workspace saved as "+name;
    }
    void EditorWorkspaceStore::LoadNamed(const std::string& name)
    {
        std::string error;
        if(!workspace::valid_name(name,error)) throw std::runtime_error(error);
        const auto source=NamedPath(name);
        if(!std::filesystem::exists(source)) throw std::runtime_error("No workspace named "+name);
        // 이름 붙인 것을 여는 것도 지금 배치를 덮는 일이다. preset 적용과 같은
        // 순서로 먼저 굳히고 백업한다.
        Save();
        workspace::backup(m_path,"before-load");
        auto document=workspace::decode(workspace::read_file(source));
        // 파일 안의 이름보다 **파일 이름**이 정본이다. 파일을 복사해 둔 사람이
        // 있으면 안쪽 이름은 남의 것을 가리킨다.
        document.name=name;
        Apply(document,false);
        RefreshNames();
        m_status.message="Workspace loaded: "+name;
    }
    void EditorWorkspaceStore::Rename(const std::string& name)
    {
        std::string error;
        if(!workspace::valid_name(name,error)) throw std::runtime_error(error);
        RefreshNames();
        if(!m_named) throw std::runtime_error("The current layout has no saved name; save it first");
        if(name==m_document.name) return;
        const auto from=NamedPath(m_document.name), to=NamedPath(name);
        // 덮어쓰기로 바꾸지 않는다. 이름을 바꾸다 남의 배치를 지우는 것은
        // 이름 바꾸기가 요구한 일이 아니다.
        if(std::filesystem::exists(to)) throw std::runtime_error("A workspace named "+name+" already exists");
        std::error_code ec;
        std::filesystem::rename(from,to,ec);
        if(ec) throw std::system_error(ec,"Cannot rename workspace");
        m_document.name=name;
        Save();
        RefreshNames();
        m_status.message="Workspace renamed to "+name;
    }
    void EditorWorkspaceStore::DeleteNamed(const std::string& name)
    {
        std::string error;
        if(!workspace::valid_name(name,error)) throw std::runtime_error(error);
        const auto target=NamedPath(name);
        if(!std::filesystem::exists(target)) throw std::runtime_error("No workspace named "+name);
        // 지운 것을 되돌릴 다른 방법이 없다.
        workspace::backup(target,"before-delete");
        std::error_code ec;
        std::filesystem::remove(target,ec);
        if(ec) throw std::system_error(ec,"Cannot delete workspace");
        // 지금 쓰는 배치를 지웠다면 **화면은 그대로 두고 이름만 놓는다.**
        // 배치를 갈아엎는 것은 지우기가 요구한 일이 아니다.
        if(name==m_document.name)
        {
            m_document.name=std::string(m_preset->label);
            m_saveRequested=true;
        }
        RefreshNames();
        m_status.message="Workspace deleted: "+name+"; backup preserved";
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
                    // 되돌리는 대상은 "기본 preset" 이 아니라 **지금 preset** 이다.
                    // Legacy Unity 를 쓰던 사람이 Reset 을 눌러 S&Box 로 튀면
                    // 그것은 복원이 아니라 다른 배치다.
                    m_document.preset=std::string(m_preset->id);
                    m_document.name=std::string(m_preset->label);
                    m_status.message="Default "+std::string(m_preset->label)+" layout restored; backup preserved";
                }
                else if(action==workspace_action::apply_preset)
                {
                    const layout_preset* target=find_layout_preset(panel);
                    if(!target) throw std::runtime_error("Unknown layout preset");
                    ApplyPreset(*target);
                }
                else if(action==workspace_action::save) m_saveRequested=true;
                else if(action==workspace_action::save_as) SaveAs(panel);
                else if(action==workspace_action::load_named) LoadNamed(panel);
                else if(action==workspace_action::rename) Rename(panel);
                else if(action==workspace_action::delete_named) DeleteNamed(panel);
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
        m_status.preset=std::string(m_preset->id);
        m_status.preset_label=std::string(m_preset->label);
        m_status.name=m_document.name;
        m_status.named=m_named;
        m_status.workspaces=m_names;
        m_status.panels.clear();
        for(const auto& entry:m_windows.entries) if(entry.persist_open) m_status.panels.emplace(entry.stable_id,entry.open);
        std::lock_guard lock(mailboxMutex);
        m_status.pending=m_saveRequested || pendingAction!=workspace_action::none;
        published=m_status;
    }
}
