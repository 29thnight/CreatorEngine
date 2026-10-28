#pragma once

#include <imgui.h>
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
    }

    // The buffer remains UTF-8 throughout editing and must outlive the widget.
    inline bool InputTextWithHintUtf8(const char* label, const char* hint, ce::utf8_string& value)
    {
        const bool changed = ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize, detail::ResizeUtf8Input, &value);
        if (changed)
        {
            const size_t length = strlen(value.data());
            value.resize_and_overwrite(length, [](char*, size_t n) noexcept { return n; });
        }
        return changed;
    }
}
