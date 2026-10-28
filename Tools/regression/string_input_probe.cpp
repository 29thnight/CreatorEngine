#include <ce/string.h>
#include "../../Editor/ImGuiHelper/Utf16Text.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    void Check(bool value, const char* contract)
    {
        if (!value)
        {
            std::fprintf(stderr, "[STRING INPUT] FAIL: %s\n", contract);
            std::exit(1);
        }
    }

    struct Context
    {
        Context()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800.0f, 600.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        ~Context()
        {
            ImGui::DestroyContext();
        }
    };

    template<class Draw>
    auto Frame(Draw draw, bool focus = false)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({700.0f, 500.0f});
        ImGui::Begin("Input contracts");
        if (focus)
        {
            ImGui::SetKeyboardFocusHere();
        }
        auto result = draw();
        ImGui::End();
        ImGui::Render();
        return result;
    }

    void Utf8Input()
    {
        Context context;
        ce::utf8_string text;
        ce::unicode_result status;
        const auto draw = [&]
        {
            return editor::text::InputTextWithHintUtf8("UTF8", "Type here", text,
                ImGuiInputTextFlags_EnterReturnsTrue, nullptr, nullptr, &status);
        };
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("hello");
        Check(!Frame(draw) && bool(status), "UTF8 typing does not submit EnterReturnsTrue");
        Check(text.view() == ce::utf8_string_view("hello") && text.size() == 5,
            "UTF8 length tracks edits within the inline buffer before Enter");
        ImGui::GetIO().AddInputCharactersUTF8(" \xed\x95\x9c\xea\xb8\x80\xf0\x9f\x98\x80");
        Frame(draw);
        Check(text.size() == 16 && bool(ce::validate_utf8(text.view())), "UTF8 Korean and supplementary input");
        const std::string bulk(8192, 'z');
        ImGui::GetIO().AddInputCharactersUTF8(bulk.c_str());
        Check(!Frame(draw), "UTF8 growth does not submit");
        Check(text.size() == 8208 && text.data()[text.size()] == 0, "UTF8 resize and NUL terminator");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        Check(Frame(draw), "UTF8 Enter submits without requiring another edit");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        Frame(draw);
    }

    struct CallbackState
    {
        int filtered = 0;
        int edited = 0;
        int completed = 0;
        int history = 0;
    };

    int Callback(ImGuiInputTextCallbackData* data)
    {
        auto& state = *static_cast<CallbackState*>(data->UserData);
        Check(data->EventFlag != ImGuiInputTextFlags_CallbackResize, "resize is never forwarded to the user");
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
        {
            ++state.filtered;
            return data->EventChar == 'x' ? 1 : 0;
        }
        if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
        {
            ++state.edited;
        }
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            ++state.completed;
            data->InsertChars(data->BufTextLen, "_completed");
        }
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            ++state.history;
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, "history");
        }
        return 0;
    }

    void Utf16Input()
    {
        Context context;
        ce::string text;
        ce::utf16_input_state state;
        CallbackState callback;
        const auto flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter |
            ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackHistory;
        const auto draw = [&]
        {
            return editor::text::InputText("UTF16", text, state, flags, Callback, &callback);
        };
        Frame(draw, true);
        Frame(draw);
        auto& io = ImGui::GetIO();
        io.AddInputCharactersUTF8("ax");
        io.AddInputCharacterUTF16(0xd55c);
        io.AddInputCharacterUTF16(0xae00);
        io.AddInputCharacterUTF16(0xd83d);
        io.AddInputCharacterUTF16(0xde00);
        auto result = Frame(draw);
        Check(result.changed && !result.submitted && bool(result.status), "UTF16 edit and submit are distinct");
        Check(text.view() == ce::string_view(u"a한글\U0001F600"), "UTF16 Korean, surrogate pair and filter callback");
        Check(callback.filtered >= 4 && callback.edited > 0, "callback receives original user data");
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        Frame(draw);
        io.AddKeyEvent(ImGuiKey_Backspace, false);
        Frame(draw);
        Check(text.view() == ce::string_view(u"a한글"), "backspace removes an entire supplementary scalar");
        io.AddKeyEvent(ImGuiKey_Tab, true);
        Frame(draw);
        io.AddKeyEvent(ImGuiKey_Tab, false);
        Frame(draw);
        Check(callback.completed == 1 && text.view() == ce::string_view(u"a한글_completed"), "completion callback edit commits");
        io.AddKeyEvent(ImGuiKey_UpArrow, true);
        Frame(draw);
        io.AddKeyEvent(ImGuiKey_UpArrow, false);
        Frame(draw);
        Check(callback.history == 1 && text.view() == ce::string_view(u"history"), "history callback edit commits");
        io.AddKeyEvent(ImGuiKey_Enter, true);
        result = Frame(draw);
        Check(result.submitted && bool(result.status), "UTF16 Enter submits current value");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame(draw);
    }

    void ConflictAndEscape()
    {
        Context context;
        ce::string text(u"original");
        ce::utf16_input_state state;
        const auto draw = [&] { return editor::text::InputText("Conflict", text, state); };
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("local");
        Frame(draw);
        text.assign(u"external");
        ImGui::GetIO().AddInputCharactersUTF8("edit");
        auto result = Frame(draw);
        Check(result.conflict && !result.changed && text.view() == ce::string_view(u"external"),
            "active local edit cannot overwrite an external change");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        Frame(draw);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        result = Frame(draw);
        Check(!result.conflict && text.view() == ce::string_view(u"external"), "blur resolves conflict to external value");
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("draft");
        Frame(draw);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        Frame(draw);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        Frame(draw);
        Check(text.view() == ce::string_view(u"external"), "Escape restores the value present at activation");
    }

    void ValidationAndMultiline()
    {
        Context context;
        ce::unicode_result status;
        ce::utf8_string bytes(ce::utf8_string_view("a\0b", 3));
        Frame([&] { return editor::text::InputTextUtf8("NUL", bytes, 0, nullptr, nullptr, &status); });
        Check(status.error == ce::unicode_error::embedded_null && status.offset == 1 && bytes.size() == 3,
            "embedded NUL is rejected without truncating owned bytes");
        bytes.assign("\xc0\x80");
        Frame([&] { return editor::text::InputTextUtf8("Invalid", bytes, 0, nullptr, nullptr, &status); });
        Check(status.error == ce::unicode_error::invalid_utf8 && bytes.size() == 2, "malformed UTF8 is rejected");
        bytes.clear();
        Frame([&] { return editor::text::InputTextUtf8("Flags", bytes,
            ImGuiInputTextFlags_CallbackAlways, nullptr, nullptr, &status); });
        Check(status.error == ce::unicode_error::unsupported_input_flags, "callback flags require an application callback");
        ce::string invalid;
        invalid.push_back(0xd800);
        ce::utf16_input_state invalidState;
        const auto rejected = Frame([&] { return editor::text::InputText("Surrogate", invalid, invalidState); });
        Check(rejected.status.error == ce::unicode_error::invalid_surrogate && invalid.size() == 1,
            "malformed UTF16 remains intact");

        ce::string text;
        ce::utf16_input_state state;
        const auto draw = [&] { return editor::text::InputTextMultiline("Multiline", text, state, {500.0f, 200.0f}); };
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("line1\nline2");
        Frame(draw);
        Check(text.view() == ce::string_view(u"line1\nline2"), "UTF16 multiline preserves newlines");
        const auto hinted = Frame([&] { return editor::text::InputTextWithHint("Hint", "Hint", text, state); });
        Check(bool(hinted.status), "UTF16 hint adapter remains supported");
        const auto utf8Draw = [&] { return editor::text::InputTextMultilineUtf8("UTF8 Multiline", bytes, {500.0f, 200.0f}); };
        Frame(utf8Draw, true);
        Frame(utf8Draw);
        ImGui::GetIO().AddInputCharactersUTF8("one\ntwo");
        Frame(utf8Draw);
        Check(bytes.view() == ce::utf8_string_view("one\ntwo"), "UTF8 multiline preserves newlines");
    }

    void SkippedInputAfterEdit()
    {
        Context context;
        char previous[32] = "";
        ce::utf8_string bytes("unchanged");
        ce::string wide(u"unchanged");
        ce::utf16_input_state state;
        bool exercised = false;
        const auto draw = [&]
        {
            const bool changed = ImGui::InputText("Previous", previous, sizeof(previous));
            if (changed)
            {
                Check(ImGui::IsItemEdited(), "skipped-input fixture needs an actual preceding edit");
                ImGui::SetWindowHiddenAndSkipItemsForCurrentFrame(ImGui::GetCurrentWindow());
                char native[32] = "unchanged";
                Check(!ImGui::InputText("Native skipped", native, sizeof(native)), "native skipped input returns false");
                Check(!editor::text::InputTextUtf8("UTF8 skipped", bytes), "skipped UTF8 must not inherit a sibling edit");
                Check(!editor::text::InputTextWithHintUtf8("Hint skipped", "hint", bytes), "skipped UTF8 hint must return false");
                Check(!editor::text::InputTextMultilineUtf8("Multiline skipped", bytes), "skipped multiline must return false");
                const auto result = editor::text::InputText("UTF16 skipped", wide, state,
                    ImGuiInputTextFlags_EnterReturnsTrue);
                Check(!result.changed && !result.submitted && bool(result.status),
                    "skipped UTF16 must not report an edit or Enter submission");
                Check(bytes.view() == ce::utf8_string_view("unchanged") && wide.view() == ce::string_view(u"unchanged"),
                    "skipped input must preserve both owned values");
                exercised = true;
            }
            return changed;
        };
        Frame(draw, true);
        Frame(draw);
        ImGui::GetIO().AddInputCharactersUTF8("edit");
        Frame(draw);
        Check(exercised, "skipped-input regression scenario did not receive its edit");
    }
}

int main()
{
    Utf8Input();
    Utf16Input();
    ConflictAndEscape();
    ValidationAndMultiline();
    SkippedInputAfterEdit();
    std::puts("STRING_INPUT_PASS utf8=utf16 enter callbacks resize unicode conflict escape multiline invalid-input skip-items");
}
