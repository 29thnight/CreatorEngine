#pragma once
#include "Utf8Text.h"
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
        bool submitted = false;
    };
    namespace detail
    {
        inline int ResizeInput(ImGuiInputTextCallbackData* data)
        {
            return ResizeUtf8Input(data);
        }
    } // namespace detail
    // UTF-8 labels remain stable widget IDs. State must outlive active editing.
    // Callbacks see ImGui's UTF-8 buffer and byte offsets. Resize is owned here.
    // changed reports edits independently from Enter submission.
    template<class Invoke>
    inline input_result Edit(const char* label, ce::string& value, ce::utf16_input_state& state,
                             ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData, Invoke invoke)
    {
        auto status = detail::ValidateInputFlags(flags, callback);
        if (!status)
        {
            return {false, false, status};
        }
        status = state.sync(value.view(), ImGui::GetActiveID() == ImGui::GetID(label));
        if (!status)
        {
            return {false, state.conflict, status};
        }
        auto& buffer = state.buffer();
        const auto edited = detail::EditUtf8(buffer, flags, callback, userData,
            [&](auto f, auto cb, auto* context) { return invoke(buffer, f, cb, context); });
        status = edited.status;
        if (edited.changed)
        {
            status = state.commit(value);
        }
        return {edited.changed && bool(status) && !state.conflict, state.conflict, status,
            edited.submitted && bool(status) && !state.conflict};
    }

    inline input_result InputText(const char* label, ce::string& value, ce::utf16_input_state& state,
                                  ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr,
                                  void* userData = nullptr)
    {
        return Edit(
            label, value, state, flags, callback, userData, [&](auto& buffer, auto f, auto cb, auto* context)
            { return ImGui::InputText(label, buffer.data(), buffer.capacity() + 1, f, cb, context); });
    }
    inline input_result InputTextWithHint(const char* label, const char* hint, ce::string& value,
                                          ce::utf16_input_state& state, ImGuiInputTextFlags flags = 0,
                                          ImGuiInputTextCallback callback = nullptr, void* userData = nullptr)
    {
        return Edit(label, value, state, flags, callback, userData,
                    [&](auto& buffer, auto f, auto cb, auto* context)
                    {
                        return ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.capacity() + 1, f,
                                                        cb, context);
                    });
    }
    inline input_result InputTextMultiline(const char* label, ce::string& value, ce::utf16_input_state& state,
                                           const ImVec2& size = {}, ImGuiInputTextFlags flags = 0,
                                           ImGuiInputTextCallback callback = nullptr, void* userData = nullptr)
    {
        if ((flags & (ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion)) != 0)
        {
            return {false, false, {ce::unicode_error::unsupported_input_flags, 0}};
        }
        return Edit(label, value, state, flags, callback, userData,
                    [&](auto& buffer, auto f, auto cb, auto* context)
                    {
                        return ImGui::InputTextMultiline(label, buffer.data(), buffer.capacity() + 1, size, f,
                                                         cb, context);
                    });
    }
} // namespace editor::text
