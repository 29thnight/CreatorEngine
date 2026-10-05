#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace wave
{
    class PlaybackService;
}

// Authored data lives on the presentation thread. Preview requests cross a
// bounded value mailbox; the host services it on the game thread only.
namespace editor::sound_graph_editing
{
    [[nodiscard]] bool CanOpen(const std::filesystem::path& path);
    [[nodiscard]] bool OpenAsset(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool CreateAsset(const std::filesystem::path& directory, std::string_view name,
        bool preset, std::filesystem::path& created, std::string& error);
    void Draw();
    void TickPreview(wave::PlaybackService* playback);
    void ShutdownPreview(wave::PlaybackService* playback);
}

namespace editor::windows
{
    void draw_sound_graph();
}
