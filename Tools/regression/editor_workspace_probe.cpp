#include "EditorWorkspaceFile.h"
#include "EditorWindowNames.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int wmain(int argc,wchar_t** argv)
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    int checks=0;
    const auto check=[&](bool ok,const char* why){if(!ok) throw std::runtime_error(why); ++checks;};
    try
    {
        check(argc==3,"fixture and temporary directories required");
        using namespace editor::workspace;
        std::vector<alias> aliases;
        for(const auto& a:EditorWindowName::legacy_names)
            for(const auto& old:{std::string(a.old_name),"###"+std::string(a.old_name)})
                aliases.push_back({old,std::string(a.stable_id),ImHashStr(old.c_str()),ImHashStr(a.stable_id.data())});
        for(const auto& a:aliases)
        {
            check(ImHashStr(("Changed label"+a.stable_id).c_str())==a.new_hash,"Label changes must preserve ID");
            check(a.old_hash!=a.new_hash,"New stable ID must not retain a legacy hash");
        }
        int fixtures=0;
        for(const auto& file:std::filesystem::directory_iterator(argv[1]))
        {
            if(file.path().extension()!=L".ini") continue;
            const auto before=read_file(file.path()), migrated=migrate_ini(before,aliases);
            check(migrated==migrate_ini(migrated,aliases),"Migration must be idempotent");
            // W4 전에는 이 이름이 그대로 남아 `###Editor.Scene` 이 됐다. 지금은 가운데
            // Host 하나로 접히므로 이주의 도달지가 `###Editor.Viewport` 다 — 실물 ini
            // 여섯에는 전부 셀이 부어지기 전의 Scene 항목이 있다.
            check(migrated.find("[Window][" + std::string(EditorWindowName::kViewport) + "]")
                != std::string::npos, "Legacy Scene must migrate to the central host id");
            std::string error; const bool valid=validate_ini(migrated,error);
            if(file.path().filename().wstring().starts_with(L"damaged-")) check(!valid,"Truncated dock graph must be rejected");
            else check(valid,error.c_str());
            check(before==read_file(file.path()),"Migration changed original fixture");
            std::cout << file.path().filename().string() << ": " << (valid?"migrated":"rejected for recovery") << "\n";
            ++fixtures;
        }
        check(fixtures==6,"Real legacy fixtures missing");
        document d; d.ini=migrate_ini(read_file(std::filesystem::path(argv[1])/L"current-debug.ini"),aliases);
        d.panels[EditorWindowName::kInspector]=false; d.panels[EditorWindowName::kHierarchy]=true;
        d.name="Saved layout \"A\""; d.tree_width=316.f;
        const auto bytes=encode(d); const auto loaded=decode(bytes);
        check(loaded.ini==d.ini && loaded.panels==d.panels && loaded.name==d.name && loaded.tree_width==316.f,"Workspace roundtrip lost state");
        const std::string unknown="\n[Window][Unknown plugin panel]\nPos=12,34\nSize=250,150\nCollapsed=0\n";
        check(migrate_ini(d.ini+unknown,aliases).find(unknown)!=std::string::npos,"Unknown window must survive migration");
        std::string duplicate=d.ini+"\n[Window]["+std::string(EditorWindowName::legacy_names[0].old_name)+"]\nPos=1,2\nSize=300,200\n";
        const auto deduped=migrate_ini(duplicate,aliases);
        const std::string hostSection="[Window][" + std::string(EditorWindowName::kViewport) + "]";
        check(deduped.find(hostSection)==deduped.rfind(hostSection),"Legacy alias created duplicate window");
        check(deduped.find("Pos=1,2\n")==std::string::npos,"Legacy alias replaced canonical layout");
        // ImGui writes `label###id` back, so migration must strip the label half.
        // Without that the saved layout is label-bound: rename a window and its dock
        // slot is lost, and a second spelling of the same window survives as a section.
        const std::string labelled="\n[Window][  Renamed Inspector###Editor.Inspector]\nPos=5,6\nSize=200,100\n";
        const auto normalized=migrate_ini(d.ini+labelled,aliases);
        check(normalized.find("###Editor.Inspector]")!=std::string::npos,"Stable id lost while stripping the label");
        check(normalized.find("Renamed")==std::string::npos,"Label half survived migration");
        check(normalized.find("[Window][###Editor.Inspector]")==normalized.rfind("[Window][###Editor.Inspector]"),
            "A relabelled window became a second section");
        std::string normalizeError;
        check(validate_ini(normalized,normalizeError),normalizeError.c_str());
        check(normalized==migrate_ini(normalized,aliases),"Label stripping is not idempotent");
        // A root is a dock node with no parent. Free docking makes a second root the
        // moment a panel is dragged out and another is docked onto it, so roots are not
        // capped at one; what is capped is the main dockspace. The builder also writes
        // the main node as a plain DockNode until DockSpace() re-flags it a frame later,
        // so the DockSpace spelling cannot be required either.
        std::string rootError;
        const std::string floating="\nDockNode      ID=0x0BADF00D Pos=40,40 Size=300,200 Selected=0x1\n";
        check(validate_ini(d.ini+floating,rootError),rootError.c_str());
        auto plainRoot=d.ini; const auto spacePos=plainRoot.find("DockSpace ");
        check(spacePos!=std::string::npos,"The fixture carries no DockSpace row to rewrite");
        plainRoot.replace(spacePos,std::string("DockSpace ").size(),"DockNode  ");
        check(validate_ini(plainRoot,rootError),rootError.c_str());
        const std::string second="\nDockSpace     ID=0x0DEFACED Window=0x1 Pos=0,0 Size=100,100\n";
        check(!validate_ini(d.ini+second,rootError),"A second main dockspace was accepted");
        const auto rejects=[&](std::string corrupt){try{(void)decode(corrupt);return false;}catch(const std::exception&){return true;}};
        auto future=bytes; future.replace(0,std::string("CreatorWorkspace 1").size(),"CreatorWorkspace 99");
        check(rejects(future),"Future schema accepted");
        check(rejects(bytes.substr(0,30)),"Truncated metadata accepted");
        auto bad=bytes; auto pos=bad.find("geometry ");bad.replace(pos,bad.find('\n',pos)-pos,"geometry 1 0 1080 220");
        check(rejects(bad),"Zero main extent accepted");
        bad=bytes;pos=bad.find("Size=");bad.replace(pos,bad.find('\n',pos)-pos,"Size=0,0");
        check(rejects(bad),"Zero editor window size accepted");
        check(rejects(bytes+"\n"+hostSection+"\nSize=100,100\n"),"Duplicate editor section accepted");
        const auto root=std::filesystem::path(argv[2])/L"워크스페이스";
        const auto target=root/L"active.workspace";
        atomic_write(target,bytes);check(read_file(target)==bytes,"Atomic save lost bytes");
        const auto copy=backup(target,"pre-test");check(read_file(copy)==bytes,"Backup lost bytes");
        d.tree_width=400.f;atomic_write(target,encode(d));check(decode(read_file(target)).tree_width==400.f,"Atomic replacement failed");
        check(read_file(copy)==bytes,"Atomic replacement overwrote backup");
        const auto copy2=backup(target,"pre-test");check(copy2!=copy && read_file(copy)==bytes,"Repeated backup overwrote history");
        std::cout << "Workspace probe PASS: " << checks << " checks, " << fixtures << " real ini fixtures\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"Workspace probe FAILED after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
