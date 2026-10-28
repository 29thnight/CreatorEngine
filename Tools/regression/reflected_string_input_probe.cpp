#include "../../Editor/ImGuiHelper/ReflectedStringInput.h"
#include <cstdio>
#include <stdexcept>

namespace
{
    unsigned checks = 0;
    void Check(bool success, const char* message)
    {
        ++checks;
        if (!success) throw std::runtime_error(message);
    }

    struct Context
    {
        Context()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800.f, 600.f};
            io.DeltaTime = 1.f / 60.f;
            io.Fonts->AddFontDefault();
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ~Context() { ImGui::DestroyContext(); }
    };

    template<class Draw>
    void Frame(Draw draw, bool focus = false)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.f, 0.f});
        ImGui::SetNextWindowSize({700.f, 500.f});
        ImGui::Begin("Reflected string input");
        if (focus) ImGui::SetKeyboardFocusHere();
        draw();
        ImGui::End();
        ImGui::Render();
    }

    template<class T>
    T Text(ce::utf8_string_view bytes)
    {
        T text;
        if constexpr (std::is_same_v<T, ce::utf8_string>) text.assign(bytes);
        else Check(bool(ce::to_utf16(bytes, text)), "invalid test UTF8");
        return text;
    }

    template<class T>
    void Transactions()
    {
        Context context;
        T value;
        T draft;
        bool changed = false, submitted = false, conflict = false;
        ce::unicode_result status;
        int commits = 0;
        auto draw = [&]
        {
            auto result = editor::text::InputReflectedString("value", value, ImGuiInputTextFlags_EnterReturnsTrue);
            draft = result.value();
            changed = result.changed;
            submitted = result.submitted;
            conflict = result.conflict;
            status = result.status;
            if (submitted)
            {
                value = result.value();
                result.accept(value);
                ++commits;
            }
        };
        Frame(draw, true);
        Frame(draw);
        auto& io = ImGui::GetIO();
        io.AddInputCharactersUTF8("한글 \xf0\x9f\x9a\x80");
        Frame(draw);
        Check(changed && !submitted && !conflict && bool(status), "typing must edit a valid draft without submission");
        Check(value.empty() && draft == Text<T>("한글 \xf0\x9f\x9a\x80"), "typing changed the model before Enter");
        const std::string bulk(4096, 'x');
        io.AddInputCharactersUTF8(bulk.c_str());
        Frame(draw);
        const T expected = draft;
        Check(value.empty() && draft.size() > 4096 && !submitted, "long reflected draft was truncated or committed early");
        Frame(draw);
        Check(draft == expected, "draft did not survive an active frame without input");
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame(draw);
        Check(submitted && commits == 1 && value == expected, "Enter did not commit the complete draft once");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame(draw);
        Check(!submitted && commits == 1, "Enter release committed the same value twice");

        Frame(draw, true);
        Frame(draw);
        value = Text<T>("external change");
        io.AddInputCharactersUTF8("local");
        Frame(draw);
        Check(conflict && !changed && !submitted, "external edit during typing did not block the stale draft");
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame(draw);
        Check(!submitted && commits == 1 && value == Text<T>("external change"), "stale Enter overwrote an external edit");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        ImGui::ClearActiveID();
        Frame(draw);
        Check(!conflict && draft == value, "leaving a conflicting edit did not reload the model");

        Frame(draw, true);
        Frame(draw);
        io.AddInputCharactersUTF8("unsaved");
        Frame(draw);
        Frame([] {}); // Removed/replaced object, same visible widget ID later.
        value = Text<T>("new object");
        Frame(draw);
        Check(!conflict && draft == value && commits == 1, "reused widget ID inherited another object's unsaved draft");
    }

    void InvalidInput()
    {
        Context context;
        ce::utf8_string nul(ce::utf8_string_view("a\0b", 3));
        bool failed = false;
        Frame([&]
        {
            auto result = editor::text::InputReflectedString("nul", nul);
            failed = !result.status && !result.changed && !result.submitted;
        });
        Check(failed && nul.size() == 3, "embedded NUL was silently truncated by reflected input");
        const char16_t invalidUnit = 0xd800;
        ce::string invalid(&invalidUnit, 1);
        Frame([&]
        {
            auto result = editor::text::InputReflectedString("surrogate", invalid);
            failed = !result.status && !result.changed;
        });
        Check(failed && invalid.size() == 1 && invalid[0] == invalidUnit, "invalid UTF16 was silently replaced in the inspector");
    }

    template<class T>
    void NormalizedCommit()
    {
        Context context;
        ImGui::GetIO().ConfigInputTextEnterKeepActive = true;
        T value;
        T draft;
        int commits = 0;
        auto draw = [&]
        {
            auto edit = editor::text::InputReflectedString("normalized", value, ImGuiInputTextFlags_EnterReturnsTrue);
            if (edit.submitted)
            {
                value = Text<T>("normalized value");
                edit.accept(value);
                ++commits;
            }
            draft = edit.value();
        };
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("raw value");
        Frame(draw);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        Frame(draw);
        Check(commits == 1 && draft == value && value == Text<T>("normalized value"),
            "property normalization did not update its accepted draft");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        Frame(draw);
        Frame(draw);
        Check(draft == value && commits == 1, "active ImGui buffer restored the value from before normalization");
    }
}

int main()
{
    try
    {
        Transactions<ce::utf8_string>();
        Transactions<ce::string>();
        InvalidInput();
        NormalizedCommit<ce::utf8_string>();
        NormalizedCommit<ce::string>();
        std::printf("[REFLECTED STRING INPUT] PASS: %u checks\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[REFLECTED STRING INPUT] FAIL: %s\n", error.what());
        return 1;
    }
}
