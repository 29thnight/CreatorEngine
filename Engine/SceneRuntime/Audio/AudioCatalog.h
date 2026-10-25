#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "PlaybackService.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace experiment::cooked
{
    class CookedAssetCatalog;
    class ArtifactByteSource;
}

namespace wave
{
    // Host/game-thread adapter. The editor calls LoadEditorAssets only after
    // EditorAssetDatabase's event revision changes. No watcher/polling thread
    // or filesystem access lives in playback or in the device callback.
    class AudioCatalog final
    {
    public:
        AudioCatalog(AudioService& audio, PlaybackService& playback);
        ~AudioCatalog();
        [[nodiscard]] bool LoadEditorAssets(const std::filesystem::path& assetsRoot, std::string& error);
        [[nodiscard]] bool LoadEditorAssets(const std::filesystem::path& assetsRoot,
            const std::filesystem::path& cacheRoot, std::string& error);
        [[nodiscard]] bool LoadCookedAssets(const experiment::cooked::CookedAssetCatalog& catalog,
            own::shared_owner<const experiment::cooked::ArtifactByteSource> bytes, std::string& error);
        [[nodiscard]] std::string ResolveLegacyClip(std::string_view name, std::string& error) const;
        void Clear();

    private:
        AudioService& m_audio;
        PlaybackService& m_playback;
        std::unordered_map<ClipKey, std::string> m_clipStamps;
        std::unordered_set<ClipKey> m_assets;
        std::unordered_map<ClipKey, own::shared_owner<const SoundGraphProgram>> m_editorGraphs;
        std::unordered_map<std::string, std::string> m_legacyAliases;
    };
}
