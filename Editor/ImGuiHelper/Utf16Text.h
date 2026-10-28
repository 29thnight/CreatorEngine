#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string.h>
// Import Creator.Containers (or ContainerAPI.h) before this adapter.
namespace editor::text
{
    inline ce::unicode_result TextUnformatted(ce::string_view value, ce::utf8_conversion_cache& cache)
    {
        auto result = cache.update(value);
        if (result)
        {
            ImGui::TextUnformatted(cache.text().data(), cache.text().data() + cache.text().size());
        }
        return result;
    }
    struct input_result
    {
        bool changed = false;
        bool conflict = false;
        ce::unicode_result status{};
    };
    namespace detail
    {
        inline int ResizeInput(ImGuiInputTextCallbackData* data)
        {
            auto& buffer = *static_cast<ce::utf8_string*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                buffer.reserve(static_cast<size_t>(data->BufSize - 1));
                buffer.resize_and_overwrite(static_cast<size_t>(data->BufTextLen),
                                            [](char*, size_t n) noexcept { return n; });
                data->Buf = buffer.data();
            }
            return 0;
        }
    } // namespace detail
    // UTF-8 labels remain stable widget IDs. State must outlive active editing.
    // Callback flags are owned by this adapter; application callbacks use a separate API.
    template<class Invoke>
    inline input_result Edit(const char* label, ce::string& value, ce::utf16_input_state& state,
                             ImGuiInputTextFlags flags, Invoke invoke)
    {
        constexpr auto callbacks = ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                                   ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter |
                                   ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit;
        if ((flags & (callbacks | ImGuiInputTextFlags_EnterReturnsTrue)) != 0)
        {
            return {false, false, {ce::unicode_error::unsupported_input_flags, 0}};
        }
        auto status = state.sync(value.view(), ImGui::GetActiveID() == ImGui::GetID(label));
        if (!status)
        {
            return {false, state.conflict, status};
        }
        auto& buffer = state.buffer();
        bool edited = invoke(buffer, flags | ImGuiInputTextFlags_CallbackResize);
        if (edited)
        {
            size_t n = strlen(buffer.data());
            buffer.resize_and_overwrite(n, [](char*, size_t length) noexcept { return length; });
            status = state.commit(value);
        }
        return {edited && bool(status) && !state.conflict, state.conflict, status};
    }

    inline input_result InputText(const char* label, ce::string& value, ce::utf16_input_state& state,
                                  ImGuiInputTextFlags flags = 0)
    {
        return Edit(
            label, value, state, flags, [&](auto& buffer, auto f)
            { return ImGui::InputText(label, buffer.data(), buffer.capacity() + 1, f, detail::ResizeInput, &buffer); });
    }
    inline input_result InputTextWithHint(const char* label, const char* hint, ce::string& value,
                                          ce::utf16_input_state& state, ImGuiInputTextFlags flags = 0)
    {
        return Edit(label, value, state, flags,
                    [&](auto& buffer, auto f)
                    {
                        return ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.capacity() + 1, f,
                                                        detail::ResizeInput, &buffer);
                    });
    }
    inline input_result InputTextMultiline(const char* label, ce::string& value, ce::utf16_input_state& state,
                                           const ImVec2& size = {}, ImGuiInputTextFlags flags = 0)
    {
        return Edit(label, value, state, flags,
                    [&](auto& buffer, auto f)
                    {
                        return ImGui::InputTextMultiline(label, buffer.data(), buffer.capacity() + 1, size, f,
                                                         detail::ResizeInput, &buffer);
                    });
    }
} // namespace editor::text
