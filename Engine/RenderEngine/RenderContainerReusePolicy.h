#pragma once

#include <cstdlib>
#include <cstring>

inline bool RenderContainerReuseEnabled()
{
    static const bool enabled = []
    {
        char* value{};
        size_t length{};
        const bool disabled =
            _dupenv_s(&value, &length, "CREATOR_RENDER_CONTAINER_REUSE") == 0 && value && std::strcmp(value, "0") == 0;
        std::free(value);
        return !disabled;
    }();
    return enabled;
}
