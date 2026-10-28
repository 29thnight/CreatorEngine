#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedSet.h"
#include "../../Engine/Containers/ContainerAPI.h"
#include "EditorWorkspaceFile.h"
#if defined(_WIN32)
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#endif
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace editor::workspace
{
    namespace
    {
        ce::utf8_string_view key(ce::utf8_string_view name)
        {
            const auto suffix = name.find("###");
            return suffix == ce::utf8_string_view::npos ? name : name.substr(suffix);
        }
        ce::utf8_string selected_key(std::uint32_t value)
        {
            char bytes[20]{};
            std::snprintf(bytes, sizeof(bytes), "Selected=0x%08X", static_cast<unsigned>(value));
            return ce::utf8_string(bytes);
        }
        void require(bool condition, const char* message)
        { if (!condition) throw std::runtime_error(message); }
    }

    ce::utf8_string migrate_ini(ce::utf8_string_view ini, const ce::dynamic_array<alias>& aliases)
    {
        require(ini.size() <= max_file_size, "Layout exceeds size limit");
        struct section { ce::utf8_string header, body, identity; bool canonical{}; };
        ce::dynamic_array<section> sections;
        std::istringstream input{std_string(ini)}; std::string line;
        const std::regex window(R"(^\[Window\]\[(.*)\]$)");
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with('['))
            {
                sections.push_back({ce::utf8_string(utf8_view(line))});
                std::smatch match;
                if (std::regex_match(line, match, window))
                {
                    auto& s = sections.back();
                    const auto raw = match[1].str();
                    const auto name = key(utf8_view(raw));
                    for (const auto& a : aliases)
                        if (name == key(a.old_name) || name == a.stable_id)
                        {
                            s.identity = a.stable_id; s.canonical = name == a.stable_id;
                            s.header.assign("[Window]["); s.header.append(a.stable_id); s.header.append("]"); break;
                        }
                    // No alias matched but the name carries a stable id: strip the label.
                    // ImGui writes `label###id` because that is what Begin received, and
                    // ImHashStr restarts at ###, so both spellings are the same window.
                    // Normalising here is what keeps a renamed window in its dock slot.
                    if (s.identity.empty() && name.starts_with("###"))
                    {
                        s.identity.assign(name); s.canonical = name == utf8_view(raw);
                        s.header.assign("[Window]["); s.header.append(name); s.header.append("]");
                    }
                }
            }
            else
            {
                if (sections.empty()) sections.push_back({});
                sections.back().body.append(utf8_view(line));
                sections.back().body.push_back('\n');
            }
        }
        // Canonical entry wins; otherwise keep the last docked alias over an undocked one.
        ce::engine_ordered_map<ce::utf8_string, std::size_t, utf8_less> winners;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& s = sections[i]; if (s.identity.empty()) continue;
            const auto it = winners.find(s.identity);
            if (it == winners.end()) winners[s.identity] = i;
            else
            {
                const auto& prior = sections[it->second];
                if ((!prior.canonical && s.canonical) || (prior.canonical == s.canonical &&
                    (s.body.find("DockId=") != ce::utf8_string_view::npos || prior.body.find("DockId=") == ce::utf8_string_view::npos)))
                    it->second = i;
            }
        }
        ce::utf8_string out;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            auto& s = sections[i];
            if (!s.identity.empty() && winners[s.identity] != i) continue;
            if (s.header == "[Docking][Data]")
                for (const auto& a : aliases)
                {
                    const auto from = selected_key(a.old_hash), to = selected_key(a.new_hash);
                    std::size_t pos = 0;
                    while ((pos = s.body.find(from, pos)) != ce::utf8_string_view::npos)
                    { s.body.erase(pos, from.size()); s.body.insert(pos, to); pos += to.size(); }
                }
            if (!s.header.empty()) { out.append(s.header); out.push_back('\n'); }
            out.append(s.body);
        }
        return out;
    }

    bool validate_ini(ce::utf8_string_view ini, ce::utf8_string& error)
    {
        error.clear();
        try
        {
            require(!ini.empty() && ini.size() <= max_file_size && ini.find(ce::utf8_string_view("\0", 1)) == ce::utf8_string_view::npos, "Empty or invalid layout");
            std::istringstream input{std_string(ini)}; std::string line;
            ce::engine_ordered_set<std::uint32_t> nodes; ce::engine_ordered_map<std::uint32_t, std::uint32_t> parents;
            ce::dynamic_array<std::uint32_t> docks; ce::engine_ordered_set<ce::utf8_string, utf8_less> windows;
            bool docking = false, known = false; int roots = 0, dockSpaces = 0;
            const std::regex id(R"(\bID=0x([0-9A-Fa-f]+))"), parent(R"(\bParent=0x([0-9A-Fa-f]+))");
            const std::regex dock(R"(^DockId=0x([0-9A-Fa-f]+))"), size(R"(^Size=(-?[0-9]+),(-?[0-9]+)$)");
            const std::regex viewport(R"(^ViewportPos=(-?[0-9]+),(-?[0-9]+)$)");
            std::smatch m;
            while (std::getline(input,line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.starts_with('['))
                {
                    require(line.ends_with(']'), "Truncated settings section");
                    docking = line == "[Docking][Data]";
                    known = line.starts_with("[Window][###Editor.");
                    if (known) require(windows.insert(ce::utf8_string(utf8_view(line))).second, "Duplicate editor window");
                }
                if (known && line.starts_with("Size="))
                {
                    require(std::regex_match(line,m,size), "Invalid window size");
                    const auto x=std::stoll(m[1]), y=std::stoll(m[2]);
                    require(x>0 && y>0 && x<=65536 && y<=65536, "Window size out of range");
                }
                // 멀티뷰포트를 켜면서 생긴 키다. 검증이 모르는 키는 무엇이 적혀 있어도
                // 통과하므로, 값의 모양과 범위를 여기서 못 박는다 — 가상 데스크톱 좌표는
                // 주 모니터 왼쪽/위가 **음수**라 부호를 허용한다.
                if (known && line.starts_with("ViewportPos="))
                {
                    require(std::regex_match(line,m,viewport), "Invalid viewport position");
                    const auto x=std::stoll(m[1]), y=std::stoll(m[2]);
                    require(x>=-65536 && x<=65536 && y>=-65536 && y<=65536, "Viewport position out of range");
                }
                if (known && std::regex_search(line,m,dock)) docks.push_back(std::stoul(m[1],nullptr,16));
                if (docking && (line.find("DockNode")!=std::string::npos || line.find("DockSpace")!=std::string::npos))
                {
                    require(std::regex_search(line,m,id), "Dock node has no ID");
                    const auto node=static_cast<std::uint32_t>(std::stoul(m[1],nullptr,16));
                    require(node!=0 && nodes.insert(node).second, "Duplicate dock node");
                    if(line.find("DockSpace")!=std::string::npos) ++dockSpaces;
                    if (std::regex_search(line,m,parent)) parents[node]=std::stoul(m[1],nullptr,16);
                    else { parents[node]=0; ++roots; }
                }
            }
            // A root is any node without a parent. Requiring exactly one, and requiring
            // the literal "DockSpace" spelling, was wrong twice over: the builder writes
            // the main node as a plain DockNode until DockSpace() re-flags it a frame
            // later, and free docking gives every floating node group its own root.
            // What must hold is that a tree exists, that it carries editor windows, and
            // that there is at most one main dockspace.
            require(roots>=1 && !windows.empty(), "Missing dock tree or editor windows");
            require(dockSpaces<=1, "More than one main dockspace");
            for (const auto dockId:docks) require(nodes.contains(dockId), "Window refers to missing dock node");
            for (const auto& [node,p]:parents)
            {
                ce::engine_ordered_set<std::uint32_t> seen; auto current=node;
                while(current)
                {
                    require(seen.insert(current).second && parents.contains(current), "Broken or cyclic dock hierarchy");
                    current=parents.at(current);
                }
            }
            return true;
        }
        catch(const std::exception& ex) { error.assign(ex.what()); return false; }
    }

    ce::utf8_string strip_viewport_positions(ce::utf8_string_view ini)
    {
        ce::utf8_string out;
        out.reserve(ini.size());
        std::istringstream input{std_string(ini)};
        std::string line;
        while (std::getline(input, line))
        {
            std::string_view view{line};
            if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
            // 줄 단위로만 본다 — 섹션을 따질 필요가 없다. 이 두 키는 `[Window]` 절에서만
            // 나오고(imgui.cpp `WindowSettingsHandler_WriteAll`), 다른 절에 같은 이름의
            // 키가 없다. 남기는 줄은 원본 그대로 옮겨 줄 끝(CRLF)을 건드리지 않는다.
            if (view.starts_with("ViewportPos=") || view.starts_with("ViewportId=")) continue;
            out.append(utf8_view(line));
            out.push_back('\n');
        }
        return out;
    }

    ce::utf8_string encode(const document& d)
    {
        std::ostringstream out;
        out << "CreatorWorkspace " << schema_version << '\n' << "name " << std::quoted(std_view(d.name))
            << "\npreset " << std::quoted(std_view(d.preset))
            << "\nviewport " << std::quoted(std_view(d.viewport)) << "\nversions " << d.theme_version << ' ' << d.imgui_version
            << "\ngeometry " << d.dpi << ' ' << d.width << ' ' << d.height << ' ' << d.tree_width
            << "\nmonitors " << std::quoted(std_view(d.monitors)) << '\n';
        for(const auto& [id,open]:d.panels) out << "panel " << std::quoted(std_view(id)) << ' ' << (open?1:0) << '\n';
        out << "--ini--\n";
        out.write(d.ini.data(), static_cast<std::streamsize>(d.ini.size()));
        return ce::utf8_string(utf8_view(out.str()));
    }
    document decode(ce::utf8_string_view bytes)
    {
        require(bytes.size()<=max_file_size, "Workspace exceeds size limit");
        const auto split=bytes.find("--ini--\n"); require(split!=ce::utf8_string_view::npos, "Workspace is truncated");
        std::istringstream in{std_string(bytes.substr(0,split))}; document d; std::string keyName; int version{};
        in >> keyName >> version;
        require(keyName=="CreatorWorkspace" && version>=oldest_readable_schema && version<=schema_version,
            "Unsupported workspace schema");
        ce::engine_ordered_set<ce::utf8_string, utf8_less> fields;
        // std::quoted input is a standard-library boundary; copy its complete
        // byte range into the engine-owned document before the temporary dies.
        const auto read_text = [&](ce::utf8_string& value)
        {
            std::string parsed;
            in >> std::quoted(parsed);
            if (!in.fail()) value.assign(utf8_view(parsed));
        };
        while(in >> keyName)
        {
            if(keyName!="panel") require(fields.insert(ce::utf8_string(utf8_view(keyName))).second, "Duplicate workspace field");
            if(keyName=="name") read_text(d.name);
            else if(keyName=="preset") read_text(d.preset);
            else if(keyName=="viewport") read_text(d.viewport);
            else if(keyName=="versions") in >> d.theme_version >> d.imgui_version;
            else if(keyName=="geometry") in >> d.dpi >> d.width >> d.height >> d.tree_width;
            else if(keyName=="monitors") read_text(d.monitors);
            else if(keyName=="panel")
            {
                ce::utf8_string id; int open{}; read_text(id); in >> open;
                require((open==0 || open==1) && d.panels.emplace(id,open==1).second, "Invalid panel state");
            }
            else throw std::runtime_error("Unknown workspace field");
            require(!in.fail(), "Malformed workspace metadata");
        }
        // v1 에는 `preset` 이 없다. 그 하나만큼 적게 요구하고 기본값을 남긴다 — 버전을
        // 하나 올렸다고 쓰던 배치를 "복구했습니다" 한 줄과 함께 버리지 않는다.
        require(fields.size()==static_cast<std::size_t>(version>=3?6:version>=2?5:4) &&
            !d.name.empty() && d.name.size()<=128 &&
            !d.preset.empty() && d.preset.size()<=64 &&
            // 지문은 **비어 있어도 된다** — 열거에 실패한 환경이 그렇게 적는다. 그때는
            // "모르는 환경" 이라 뷰포트 좌표를 못 믿는 쪽으로 가므로 거절할 이유가 없다.
            d.monitors.size()<=512, "Missing workspace metadata");
        require(std::isfinite(d.dpi) && d.dpi>=0.5f && d.dpi<=8.f && std::isfinite(d.width) && d.width>0.f && d.width<=65536.f &&
            std::isfinite(d.height) && d.height>0.f && d.height<=65536.f && std::isfinite(d.tree_width) && d.tree_width>=140.f && d.tree_width<=600.f,
            "Invalid workspace geometry");
        d.ini.assign(bytes.substr(split+8)); ce::utf8_string error;
        const bool valid = validate_ini(d.ini,error);
        require(valid, error.c_str()); return d;
    }
    ce::utf8_string read_file(const std::filesystem::path& path)
    {
        require(std::filesystem::file_size(path)<=max_file_size, "Workspace exceeds size limit");
        std::ifstream in(path,std::ios::binary); require(bool(in),"Cannot open workspace");
        ce::utf8_string bytes;
        char block[4096];
        while (in.read(block, sizeof(block)) || in.gcount() != 0)
        {
            const auto count = static_cast<std::size_t>(in.gcount());
            require(count <= max_file_size - bytes.size(), "Workspace exceeds size limit");
            bytes.append(ce::utf8_string_view(block, count));
        }
        require(!in.bad(),"Cannot read workspace"); return bytes;
    }
#if defined(_WIN32)
    void atomic_write(const std::filesystem::path& path, ce::utf8_string_view bytes)
    {
        require(bytes.size()<=max_file_size,"Workspace exceeds size limit");
        std::filesystem::create_directories(path.parent_path());
        const auto temp=path.wstring()+L".candidate";
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file!=INVALID_HANDLE_VALUE,"Workspace candidate already exists or cannot be created");
        DWORD written{}; const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)
            && written==bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if(!ok) { DeleteFileW(temp.c_str()); throw std::runtime_error("Workspace write/flush failed"); }
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        { DeleteFileW(temp.c_str()); throw std::runtime_error("Workspace atomic replacement failed"); }
    }
#endif

    bool valid_name(ce::utf8_string_view name, ce::utf8_string& error)
    {
        error.clear();
        const auto fail = [&](ce::utf8_string_view message) { error.assign(message); return false; };
        if(name.empty()) return fail("Workspace name is empty");
        if(name.size()>64) return fail("Workspace name is longer than 64 bytes");
        // UI and CLI names share one UTF-8 contract. Reject malformed bytes before
        // a filesystem conversion, and never replace invalid text silently.
        ce::string decoded;
        if (!ce::to_utf16(name, decoded)) return fail("Workspace name is not valid UTF-8");
        for(std::size_t i = 0; i < name.size(); ++i)
        {
            const char c = name[i];
            const unsigned char u=static_cast<unsigned char>(c);
            if(u<0x20 || u==0x7f) return fail("Workspace name contains a control character");
            if(std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)
            {
                error.assign("Workspace name contains a reserved character: ");
                error.push_back(c);
                return false;
            }
        }
        // Preserve the byte limit and filename policy used by existing files.
        if(name[0]==' ' || name[name.size()-1]==' ' || name[0]=='.' || name[name.size()-1]=='.')
            return fail("Workspace name begins or ends with a space or a dot");
        ce::utf8_string upper(name);
        for(std::size_t i = 0; i < upper.size(); ++i)
            if (upper[i] >= 'a' && upper[i] <= 'z') upper[i] = static_cast<char>(upper[i] - 'a' + 'A');
        const auto is_device = [&]
        {
            if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return true;
            return upper.size() == 4 && (upper.view().starts_with("COM") || upper.view().starts_with("LPT")) &&
                upper[3] >= '1' && upper[3] <= '9';
        };
        if(is_device()) return fail("Workspace name is a reserved device name");
        if(upper=="ACTIVE") return fail("Workspace name 'active' is reserved");
        return true;
    }

    std::filesystem::path backup(const std::filesystem::path& path, ce::utf8_string_view reason)
    {
        if(!std::filesystem::exists(path)) return {};
        for(unsigned i=0;i<10000;++i)
        {
            ce::utf8_string suffix("."); suffix.append(reason);
            if (i)
            {
                char ordinal[16]{}; std::snprintf(ordinal, sizeof(ordinal), ".%u", i);
                suffix.append(ordinal);
            }
            auto target=path;
            target += std::u8string(suffix.data(), suffix.data() + suffix.size());
            std::error_code ec;
            if(std::filesystem::copy_file(path,target,std::filesystem::copy_options::none,ec)) return target;
            if(ec!=std::errc::file_exists) throw std::system_error(ec,"Cannot preserve workspace backup");
        }
        throw std::runtime_error("Workspace backup limit exceeded");
    }
}
