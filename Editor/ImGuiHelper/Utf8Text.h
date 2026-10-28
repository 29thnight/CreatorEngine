#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <limits.h>
#include <string.h>

// Import Creator.Containers (or ContainerAPI.h) before this adapter.
namespace editor::text
{
    namespace detail
    {
        inline int ResizeUtf8Input(ImGuiInputTextCallbackData* data)
        {
            auto& value = *static_cast<ce::utf8_string*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                value.reserve(static_cast<size_t>(data->BufSize - 1));
                value.resize_and_overwrite(static_cast<size_t>(data->BufTextLen),
                    [](char*, size_t length) noexcept { return length; });
                data->Buf = value.data();
            }
            return 0;
        }
        struct input_callback
        {
            ce::utf8_string& value;
            ImGuiInputTextCallback callback;
            void* userData;
        };

        inline int ForwardInputCallback(ImGuiInputTextCallbackData* data)
        {
            auto& context = *static_cast<input_callback*>(data->UserData);
            void* original = data->UserData;
            int result = 0;
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                data->UserData = &context.value;
                result = ResizeUtf8Input(data);
            }
            else if (context.callback != nullptr)
            {
                data->UserData = context.userData;
                result = context.callback(data);
            }
            data->UserData = original;
            return result;
        }

        inline ce::unicode_result ValidateInputFlags(ImGuiInputTextFlags flags, ImGuiInputTextCallback callback)
        {
            constexpr auto callbacks = ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter |
                ImGuiInputTextFlags_CallbackEdit;
            if ((flags & ImGuiInputTextFlags_CallbackResize) != 0 ||
                ((flags & callbacks) != 0 && callback == nullptr))
            {
                return {ce::unicode_error::unsupported_input_flags, 0};
            }
            return {};
        }

        inline ce::unicode_result ValidateUtf8Input(const ce::utf8_string& value)
        {
            if (value.capacity() >= static_cast<size_t>(INT_MAX))
            {
                return {ce::unicode_error::input_too_large, value.size()};
            }
            const auto* zero = static_cast<const char*>(memchr(value.data(), 0, value.size()));
            if (zero != nullptr)
            {
                return {ce::unicode_error::embedded_null, static_cast<size_t>(zero - value.data())};
            }
            return ce::validate_utf8(value.view());
        }

        struct utf8_input_result
        {
            bool changed = false;
            bool submitted = false;
            ce::unicode_result status{};
        };

        template<class Invoke>
        inline utf8_input_result EditUtf8(ce::utf8_string& value, ImGuiInputTextFlags flags,
            ImGuiInputTextCallback callback, void* userData, Invoke invoke)
        {
            auto status = ValidateInputFlags(flags, callback);
            if (!status)
            {
                return {false, false, status};
            }
            status = ValidateUtf8Input(value);
            if (!status)
            {
                return {false, false, status};
            }

            // InputTextEx returns before submitting an item when the window
            // skips items. LastItemData may still describe an edited sibling.
            if (ImGui::GetCurrentWindow()->SkipItems)
            {
                return {};
            }
            input_callback context{value, callback, userData};
            const bool returned = invoke(flags | ImGuiInputTextFlags_CallbackResize, ForwardInputCallback, &context);
            const bool changed = (flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0
                ? ImGui::IsItemEdited() : returned;
            // EnterReturnsTrue reports submission, not every edit. Synchronize
            // the owned length even when ImGui returns false or no growth occurs.
            const size_t length = strlen(value.data());
            value.resize_and_overwrite(length, [](char*, size_t n) noexcept { return n; });
            status = ValidateUtf8Input(value);
            return {changed && bool(status),
                (flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0 && returned && bool(status), status};
        }

        inline bool InputReturn(const utf8_input_result& result, ImGuiInputTextFlags flags,
            ce::unicode_result* status)
        {
            if (status != nullptr)
            {
                *status = result.status;
            }
            return (flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0 ? result.submitted : result.changed;
        }
    }

    // The UTF-8 buffer must outlive active editing. The caller owns external-edit
    // arbitration. Callbacks retain ImGui's UTF-8 byte offsets; CallbackResize
    // belongs to this adapter. Inspect status for invalid input.
    inline bool InputTextUtf8(const char* label, ce::utf8_string& value, ImGuiInputTextFlags flags = 0,
        ImGuiInputTextCallback callback = nullptr, void* userData = nullptr, ce::unicode_result* status = nullptr)
    {
        const auto result = detail::EditUtf8(value, flags, callback, userData,
            [&](auto f, auto cb, auto* context)
            { return ImGui::InputText(label, value.data(), value.capacity() + 1, f, cb, context); });
        return detail::InputReturn(result, flags, status);
    }

    inline bool InputTextWithHintUtf8(const char* label, const char* hint, ce::utf8_string& value,
        ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr,
        ce::unicode_result* status = nullptr)
    {
        const auto result = detail::EditUtf8(value, flags, callback, userData,
            [&](auto f, auto cb, auto* context)
            { return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, f, cb, context); });
        return detail::InputReturn(result, flags, status);
    }

    inline bool InputTextMultilineUtf8(const char* label, ce::utf8_string& value, const ImVec2& size = {},
        ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr,
        ce::unicode_result* status = nullptr)
    {
        if ((flags & (ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion)) != 0)
        {
            if (status != nullptr)
            {
                *status = {ce::unicode_error::unsupported_input_flags, 0};
            }
            return false;
        }
        const auto result = detail::EditUtf8(value, flags, callback, userData,
            [&](auto f, auto cb, auto* context)
            { return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size, f, cb, context); });
        return detail::InputReturn(result, flags, status);
    }
}
