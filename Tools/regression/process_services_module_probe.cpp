import std;
import Creator.Core.Progress;
import Creator.Core.EngineMode;

ce::progress::sink* sink_from_other_unit();
EngineRunMode mode_from_other_unit();
void launch_from_other_unit();

namespace
{
    unsigned checks{};
    void require(bool condition, std::string_view label)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string(label));
        }
    }
}
int main(int argc, char** argv)
{
    try
    {
        const bool player = argc == 2 && std::string_view{argv[1]} == "player";
        const auto expected = player ? EngineRunMode::Player : EngineRunMode::Editor;
        EngineMode::Set(expected);
        require(EngineMode::Get() == expected, "selected mode");
        require(mode_from_other_unit() == expected, "mode state shared across translation units");
        require(EngineMode::IsEditor() == !player && EngineMode::IsPlayer() == player, "mode predicates");
        EngineMode::Set(expected);
        require(EngineMode::Get() == expected, "repeated same-mode setting");
        auto& state = ce::progress::get_sink();
        require(&state == sink_from_other_unit(), "single registry across translation units");
        require(!state.launch && !state.set_title && !state.set_status && !state.set_progress && !state.close, "empty registry");
        ce::progress::launch();
        ce::progress::set_title(L"ignored");
        ce::progress::set_status(L"ignored");
        ce::progress::set_progress(0.5f);
        ce::progress::close();
        require(!state.launch && !state.close, "empty registry calls are no-ops");
        unsigned launched{}, closed{};
        std::wstring title, status;
        float progress{};
        state = ce::progress::sink{
            [&] { ++launched; },
            [&](const std::wstring& value) { title = value; },
            [&](const std::wstring& value) { status = value; },
            [&](float value) { progress = value; },
            [&] { ++closed; }};
        launch_from_other_unit();
        require(launched == 1, "callback publication across translation units");
        const std::wstring input{L"A\0B", 3};
        ce::progress::set_title(input);
        ce::progress::set_status(L" loading \n");
        ce::progress::set_progress(1.25f);
        ce::progress::close();
        require(title == input, "title preserves embedded NUL");
        require(status == L" loading \n", "status preserves whitespace");
        require(progress == 1.25f, "progress forwards the original value");
        require(closed == 1, "close callback");
        unsigned replaced{};
        state.launch = [&] { ++replaced; };
        ce::progress::launch();
        require(replaced == 1 && launched == 1, "registry callback replacement");
        state = {};
        ce::progress::launch();
        ce::progress::close();
        require(replaced == 1 && closed == 1 && sink_from_other_unit() == &state, "registry clearing keeps identity");
        std::cout << "PROCESS_SERVICES_MODULES_OK mode=" << (player ? "Player" : "Editor") << " checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
