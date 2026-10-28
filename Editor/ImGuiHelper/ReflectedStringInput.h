#pragma once

#include "../../Engine/Containers/ContainerAPI.h"
#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "Utf16Text.h"

namespace editor::text
{
    template<class T>
    struct reflected_string_state
    {
        T original;
        T draft;
        ce::utf16_input_state utf16;
        int lastSeenFrame = -1;
        bool conflict = false;

        void reset(const T& value)
        {
            original.assign(value.view());
            draft.assign(value.view());
            utf16 = {};
            conflict = false;
        }
    };

    template<class T>
    struct reflected_string_result
    {
        reflected_string_state<T>& state;
        ImGuiID id;
        bool changed;
        bool submitted;
        bool conflict;
        ce::unicode_result status;

        const T& value() const noexcept { return state.draft; }

        void accept(const T& committed)
        {
            // A property-change hook may normalize the submitted value.
            state.original.assign(committed.view());
            if (state.draft != committed)
            {
                state.draft.assign(committed.view());
                state.utf16 = {};
                if (auto* input = ImGui::GetInputTextState(id))
                    input->ReloadUserBufAndKeepSelection();
            }
            state.conflict = false;
        }
    };

    // Caller supplies a stable ImGui ID scope (object + member/element). Drafts
    // only survive consecutive frames of active editing. Context changes and a
    // gap in drawing reset them, preventing collapsed/replaced objects from
    // inheriting an old unsaved draft. Inactive entries are periodically pruned.
    template<class T>
        requires (std::is_same_v<T, ce::utf8_string> || std::is_same_v<T, ce::string>)
    inline reflected_string_result<T> InputReflectedString(const char* label, const T& source,
        ImGuiInputTextFlags flags = 0)
    {
        static ImGuiContext* context = nullptr;
        static ce::engine_node_map<ImGuiID, reflected_string_state<T>> states;
        static int lastCleanupFrame = 0;
        const int frame = ImGui::GetFrameCount();
        if (context != ImGui::GetCurrentContext() || frame < lastCleanupFrame)
        {
            states.clear();
            context = ImGui::GetCurrentContext();
            lastCleanupFrame = frame;
        }
        if (frame - lastCleanupFrame >= 300)
        {
            for (auto it = states.begin(); it != states.end();)
            {
                if (frame - it->second.lastSeenFrame > 600 && it->first != ImGui::GetActiveID())
                    it = states.erase(it);
                else
                    ++it;
            }
            lastCleanupFrame = frame;
        }

        const ImGuiID id = ImGui::GetID(label);
        auto& state = states[id];
        const bool gap = state.lastSeenFrame != frame && state.lastSeenFrame != frame - 1;
        if (gap && ImGui::GetActiveID() == id)
            ImGui::ClearActiveID();
        const bool active = ImGui::GetActiveID() == id;
        if (!active || gap)
        {
            if (gap || state.conflict || state.original != source || state.draft != source)
                state.reset(source);
            // ImGui can replay a final edit after deactivation. An abandoned
            // draft must not be applied to a replacement object with this ID.
            auto& deactivated = ImGui::GetCurrentContext()->InputTextDeactivatedState;
            if (deactivated.ID == id) deactivated.ID = 0;
            if (gap)
                if (auto* input = ImGui::GetInputTextState(id)) input->ID = 0;
        }
        else if (state.original != source)
            state.conflict = true;
        state.lastSeenFrame = frame;

        bool changed = false;
        bool submitted = false;
        ce::unicode_result status;
        if constexpr (std::is_same_v<T, ce::utf8_string>)
        {
            const auto result = detail::EditUtf8(state.draft, flags, nullptr, nullptr,
                [&](auto f, auto cb, auto* callbackContext)
                { return ImGui::InputText(label, state.draft.data(), state.draft.capacity() + 1, f, cb, callbackContext); });
            changed = result.changed;
            submitted = result.submitted;
            status = result.status;
        }
        else
        {
            const auto result = InputText(label, state.draft, state.utf16, flags);
            changed = result.changed;
            submitted = result.submitted;
            status = result.status;
            state.conflict = state.conflict || result.conflict;
        }
        return {state, id, changed && !state.conflict, submitted && !state.conflict,
            state.conflict, status};
    }

    inline void DrawStringInputIssue(ce::unicode_result status, bool conflict = false)
    {
        if (conflict)
            ImGui::TextDisabled("Value changed externally. Leave the input to reload it.");
        else if (!status)
            ImGui::TextDisabled("Text cannot be edited (encoding error at %zu).", status.offset);
    }
}
