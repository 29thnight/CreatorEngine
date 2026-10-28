#ifndef CE_CONTRACT_STANDALONE
#define CE_CONTRACT_STANDALONE 1
#endif
#include "../../Editor/ImGuiHelper/NodeEditor.h"

namespace
{
    void Require(bool condition, const char* description)
    {
        if (!condition) throw std::runtime_error(description);
    }

    ce::utf8_string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ce::utf8_string(ce::utf8_string_view(bytes.data(), bytes.size()));
    }

    template<class Draw>
    void Frame(Draw&& draw)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 600.0f});
        ImGui::Begin("Node UTF-8 storage");
        draw();
        ImGui::End();
        ImGui::Render();
    }

    void CheckSettingsAndNames(const std::filesystem::path& directory)
    {
        const auto originalPath = NodeEditor::SettingsPath(directory, "한글-🎹.json");
        const auto renamedPath = NodeEditor::SettingsPath(directory, "더 긴 이름-🎹-노드.json");
        Require(originalPath && renamedPath, "Unicode settings filenames are accepted");
        Require(originalPath->filename().u8string() == u8"한글-🎹.json", "native filename keeps UTF-8 text");
        Require(!NodeEditor::SettingsPath(directory, {}) &&
            !NodeEditor::SettingsPath(directory, ce::utf8_string_view("a\0b", 3)) &&
            !NodeEditor::SettingsPath(directory, ce::utf8_string_view("\xC0\xAF", 2)),
            "empty, NUL and malformed UTF-8 filenames are rejected");

        ce::utf8_string longName;
        for (int index = 0; index < 64; ++index) longName.append("한글🎹");
        const ce::utf8_string_view firstName("대기 %s##state");
        const ImVec2 savedPosition(192.0f, 96.0f);
        {
            NodeEditor editor;
            Frame([&] {
                Require(editor.MakeEdit(*originalPath), "open node editor");
                editor.MakeNode(firstName);
                editor.MakeNode(longName.view());
                Require(editor.FindNode(longName.view()) != nullptr &&
                    editor.FindNode("missing") == nullptr, "node lookup keeps full UTF-8 names");
                editor.MakeLink(firstName, longName.view());
                editor.MakeLink(longName.view(), firstName);
                editor.MakeLink("missing", firstName);
                Require(editor.Links.size() == 2 && editor.Links.back()->haveReverse,
                    "valid and reverse links resolve exact names; missing endpoints are ignored");
                editor.DrawNode();
                editor.EndEdit();
            });

            ed::SetCurrentEditor(editor.m_nodeContext);
            ed::SetNodePosition(1000, savedPosition);
            ed::SetCurrentEditor(nullptr);
            Frame([&] {
                Require(editor.MakeEdit(*originalPath), "reuse node context");
                editor.MakeNode(firstName);
                editor.MakeNode(longName.view());
                editor.DrawNode();
                editor.EndEdit();
            });
            Require(std::filesystem::exists(*originalPath), "real node-editor JSON is written through native path");
            const auto originalJson = Read(*originalPath);
            Require(!originalJson.empty() && originalJson.find("nodes") != ce::utf8_string_view::npos,
                "real library serialized the graph");

            const auto& config = ed::GetConfig(editor.m_nodeContext);
            Require(config.SettingsFile == nullptr && config.LoadSettings && config.SaveSettings &&
                config.UserPointer == &editor, "native callbacks own the filename boundary");
            const auto size = config.LoadSettings(nullptr, config.UserPointer);
            Require(size == originalJson.size(), "load advertises the captured byte count");
            const ce::utf8_string replacement(ce::utf8_string_view("{\"changed\":\"한글🎹\"}"));
            Require(config.SaveSettings(replacement.data(), replacement.size(), ed::SaveReasonFlags::User,
                config.UserPointer), "write a different file between the load callbacks");
            ce::dynamic_array<char> buffer(size + 1, '!');
            Require(config.LoadSettings(buffer.data(), config.UserPointer) == size && buffer[size] == '!' &&
                ce::utf8_string_view(buffer.data(), size) == originalJson.view(),
                "two-step load returns the owned snapshot without overrunning the caller");
            Require(config.SaveSettings(originalJson.data(), originalJson.size(), ed::SaveReasonFlags::User,
                config.UserPointer), "restore graph JSON");

            auto* context = editor.m_nodeContext;
            Require(editor.ReNameJson(*renamedPath) && editor.m_nodeContext == context,
                "renaming preserves the live graph context");
            Require(!std::filesystem::exists(*originalPath) && std::filesystem::exists(*renamedPath),
                "native rename moves the Unicode settings file");
            Require(config.SaveSettings(originalJson.data(), originalJson.size(), ed::SaveReasonFlags::User,
                config.UserPointer) && !std::filesystem::exists(*originalPath),
                "the retained config saves to the renamed path without a dangling string pointer");

            const auto occupied = directory / "occupied.json";
            { std::ofstream file(occupied); file << "keep"; }
            Require(!editor.ReNameJson(occupied) && editor.m_filePath == *renamedPath && Read(occupied) == "keep",
                "rename collision preserves both the current path and existing destination");
        }

        NodeEditor reloaded;
        Frame([&] {
            Require(reloaded.MakeEdit(*renamedPath), "load renamed settings with a fresh context");
            reloaded.MakeNode(firstName);
            reloaded.MakeNode(longName.view());
            reloaded.DrawNode();
            const auto position = ed::GetNodePosition(1000);
            Require(position.x == savedPosition.x && position.y == savedPosition.y,
                "native JSON save/load preserves node layout after a Unicode rename");
            reloaded.EndEdit();
        });

        // A different settings path can represent a recycled controller slot.
        // Its selection and in-progress link must not reach the previous graph.
        int pendingTarget = -1;
        reloaded.m_selectedType = SelectedType::Node;
        reloaded.seletedCurNodeIndex = 1;
        reloaded.MakeNewLink(&pendingTarget);
        Frame([&] {
            Require(reloaded.MakeEdit(directory / "another-controller.json"), "change native settings owner");
            Require(reloaded.m_selectedType == SelectedType::None && reloaded.seletedCurNodeIndex == -1 &&
                !reloaded.needMakeLink && reloaded.m_retrunIndex == nullptr,
                "new settings owner retained selection or a pending link");
            reloaded.seletedCurNodeIndex = 4;
            reloaded.MakeNewLink(&pendingTarget);
            reloaded.Update();
            Require(!reloaded.needMakeLink && reloaded.m_retrunIndex == nullptr && pendingTarget == -1,
                "missing selected node did not cancel an in-progress link");
            reloaded.EndEdit();
        });
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected scratch output directory");
        const auto directory = std::filesystem::path(argv[1]) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(directory);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800.0f, 600.0f};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        CheckSettingsAndNames(directory);
        ImGui::DestroyContext();
        std::puts("NODE_EDITOR_STRINGS_OK");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NODE_EDITOR_STRINGS_FAILED: %s\n", error.what());
        return 1;
    }
}
