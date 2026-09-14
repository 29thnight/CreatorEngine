#include "EditorWorkspaceFile.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace editor::workspace
{
    namespace
    {
        std::string key(std::string name)
        {
            const auto suffix = name.find("###");
            return suffix == std::string::npos ? name : name.substr(suffix);
        }
        std::string hex(std::uint32_t value)
        {
            std::ostringstream out; out << "0x" << std::uppercase << std::hex
                << std::setw(8) << std::setfill('0') << value; return out.str();
        }
        void require(bool condition, const char* message)
        { if (!condition) throw std::runtime_error(message); }
    }

    std::string migrate_ini(std::string_view ini, const std::vector<alias>& aliases)
    {
        require(ini.size() <= max_file_size, "Layout exceeds size limit");
        struct section { std::string header, body, identity; bool canonical{}; };
        std::vector<section> sections;
        std::istringstream input{std::string(ini)}; std::string line;
        const std::regex window(R"(^\[Window\]\[(.*)\]$)");
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with('['))
            {
                sections.push_back({line});
                std::smatch match;
                if (std::regex_match(line, match, window))
                {
                    auto& s = sections.back();
                    const auto raw = match[1].str(), name = key(raw);
                    for (const auto& a : aliases)
                        if (name == key(a.old_name) || name == a.stable_id)
                        {
                            s.identity = a.stable_id; s.canonical = name == a.stable_id;
                            s.header = "[Window][" + a.stable_id + "]"; break;
                        }
                    // No alias matched but the name carries a stable id: strip the label.
                    // ImGui writes `label###id` because that is what Begin received, and
                    // ImHashStr restarts at ###, so both spellings are the same window.
                    // Normalising here is what keeps a renamed window in its dock slot.
                    if (s.identity.empty() && name.starts_with("###"))
                    {
                        s.identity = name; s.canonical = name == raw;
                        s.header = "[Window][" + name + "]";
                    }
                }
            }
            else
            {
                if (sections.empty()) sections.push_back({});
                sections.back().body += line + '\n';
            }
        }
        // Canonical entry wins; otherwise keep the last docked alias over an undocked one.
        std::map<std::string, std::size_t> winners;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto& s = sections[i]; if (s.identity.empty()) continue;
            const auto it = winners.find(s.identity);
            if (it == winners.end()) winners[s.identity] = i;
            else
            {
                const auto& prior = sections[it->second];
                if ((!prior.canonical && s.canonical) || (prior.canonical == s.canonical &&
                    (s.body.find("DockId=") != std::string::npos || prior.body.find("DockId=") == std::string::npos)))
                    it->second = i;
            }
        }
        std::string out;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            auto& s = sections[i];
            if (!s.identity.empty() && winners[s.identity] != i) continue;
            if (s.header == "[Docking][Data]")
                for (const auto& a : aliases)
                {
                    const auto from = "Selected=" + hex(a.old_hash), to = "Selected=" + hex(a.new_hash);
                    std::size_t pos = 0;
                    while ((pos = s.body.find(from, pos)) != std::string::npos)
                    { s.body.replace(pos, from.size(), to); pos += to.size(); }
                }
            if (!s.header.empty()) out += s.header + '\n';
            out += s.body;
        }
        return out;
    }

    bool validate_ini(std::string_view ini, std::string& error)
    {
        error.clear();
        try
        {
            require(!ini.empty() && ini.size() <= max_file_size && ini.find('\0') == std::string::npos, "Empty or invalid layout");
            std::istringstream input{std::string(ini)}; std::string line;
            std::set<std::uint32_t> nodes; std::map<std::uint32_t, std::uint32_t> parents;
            std::vector<std::uint32_t> docks; std::set<std::string> windows;
            bool docking = false, known = false; int roots = 0, dockSpaces = 0;
            const std::regex id(R"(\bID=0x([0-9A-Fa-f]+))"), parent(R"(\bParent=0x([0-9A-Fa-f]+))");
            const std::regex dock(R"(^DockId=0x([0-9A-Fa-f]+))"), size(R"(^Size=(-?[0-9]+),(-?[0-9]+)$)");
            std::smatch m;
            while (std::getline(input,line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.starts_with('['))
                {
                    require(line.ends_with(']'), "Truncated settings section");
                    docking = line == "[Docking][Data]";
                    known = line.starts_with("[Window][###Editor.");
                    if (known) require(windows.insert(line).second, "Duplicate editor window");
                }
                if (known && line.starts_with("Size="))
                {
                    require(std::regex_match(line,m,size), "Invalid window size");
                    const auto x=std::stoll(m[1]), y=std::stoll(m[2]);
                    require(x>0 && y>0 && x<=65536 && y<=65536, "Window size out of range");
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
                std::set<std::uint32_t> seen; auto current=node;
                while(current)
                {
                    require(seen.insert(current).second && parents.contains(current), "Broken or cyclic dock hierarchy");
                    current=parents.at(current);
                }
            }
            return true;
        }
        catch(const std::exception& ex) { error=ex.what(); return false; }
    }

    std::string encode(const document& d)
    {
        std::ostringstream out;
        out << "CreatorWorkspace " << schema_version << '\n' << "name " << std::quoted(d.name)
            << "\npreset " << std::quoted(d.preset)
            << "\nviewport " << std::quoted(d.viewport) << "\nversions " << d.theme_version << ' ' << d.imgui_version
            << "\ngeometry " << d.dpi << ' ' << d.width << ' ' << d.height << ' ' << d.tree_width << '\n';
        for(const auto& [id,open]:d.panels) out << "panel " << std::quoted(id) << ' ' << (open?1:0) << '\n';
        out << "--ini--\n" << d.ini; return out.str();
    }
    document decode(std::string_view bytes)
    {
        require(bytes.size()<=max_file_size, "Workspace exceeds size limit");
        const auto split=bytes.find("--ini--\n"); require(split!=std::string::npos, "Workspace is truncated");
        std::istringstream in{std::string(bytes.substr(0,split))}; document d; std::string keyName; int version{};
        in >> keyName >> version;
        require(keyName=="CreatorWorkspace" && version>=oldest_readable_schema && version<=schema_version,
            "Unsupported workspace schema");
        std::set<std::string> fields;
        while(in >> keyName)
        {
            if(keyName!="panel") require(fields.insert(keyName).second, "Duplicate workspace field");
            if(keyName=="name") in >> std::quoted(d.name);
            else if(keyName=="preset") in >> std::quoted(d.preset);
            else if(keyName=="viewport") in >> std::quoted(d.viewport);
            else if(keyName=="versions") in >> d.theme_version >> d.imgui_version;
            else if(keyName=="geometry") in >> d.dpi >> d.width >> d.height >> d.tree_width;
            else if(keyName=="panel")
            {
                std::string id; int open{}; in >> std::quoted(id) >> open;
                require((open==0 || open==1) && d.panels.emplace(id,open==1).second, "Invalid panel state");
            }
            else throw std::runtime_error("Unknown workspace field");
            require(!in.fail(), "Malformed workspace metadata");
        }
        // v1 에는 `preset` 이 없다. 그 하나만큼 적게 요구하고 기본값을 남긴다 — 버전을
        // 하나 올렸다고 쓰던 배치를 "복구했습니다" 한 줄과 함께 버리지 않는다.
        require(fields.size()==static_cast<std::size_t>(version>=2?5:4) &&
            !d.name.empty() && d.name.size()<=128 &&
            !d.preset.empty() && d.preset.size()<=64, "Missing workspace metadata");
        require(std::isfinite(d.dpi) && d.dpi>=0.5f && d.dpi<=8.f && std::isfinite(d.width) && d.width>0.f && d.width<=65536.f &&
            std::isfinite(d.height) && d.height>0.f && d.height<=65536.f && std::isfinite(d.tree_width) && d.tree_width>=140.f && d.tree_width<=600.f,
            "Invalid workspace geometry");
        d.ini=bytes.substr(split+8); std::string error;
        require(validate_ini(d.ini,error), error.c_str()); return d;
    }
    std::string read_file(const std::filesystem::path& path)
    {
        require(std::filesystem::file_size(path)<=max_file_size, "Workspace exceeds size limit");
        std::ifstream in(path,std::ios::binary); require(bool(in),"Cannot open workspace");
        std::string bytes{std::istreambuf_iterator<char>(in),{}};
        require(!in.bad(),"Cannot read workspace"); return bytes;
    }
    void atomic_write(const std::filesystem::path& path, std::string_view bytes)
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
    std::filesystem::path backup(const std::filesystem::path& path, std::string_view reason)
    {
        if(!std::filesystem::exists(path)) return {};
        for(unsigned i=0;i<10000;++i)
        {
            auto target=path; target += "."+std::string(reason)+(i?"."+std::to_string(i):"");
            std::error_code ec;
            if(std::filesystem::copy_file(path,target,std::filesystem::copy_options::none,ec)) return target;
            if(ec!=std::errc::file_exists) throw std::system_error(ec,"Cannot preserve workspace backup");
        }
        throw std::runtime_error("Workspace backup limit exceeded");
    }
}
