#include "AudioProfileProvider.h"
#include "AudioHost.h"
#include "PlaybackService.h"
#include "../../EngineDiagnostics/ProfileScope.h"

#include <array>

namespace wave
{
    void PublishAudioProfile(const AudioHost& host, const PlaybackService& playback)
    {
#if !CE_SHIPPING
        auto& profiler = ce::profiler();
        if (!profiler.counter_enabled(ce::counter_category::audio))
        {
            return;
        }
        static constexpr std::array names{
            "Audio.Active voices", "Audio.Physical voices", "Audio.Virtual voices", "Audio.Paused voices",
            "Audio.Stolen total", "Audio.Dropped total", "Audio.Backend failures total", "Audio.Playback instances",
            "Audio.Runtime update", "Audio.Stream bytes total", "Audio.Stream read failures total",
            "Audio.Callback p99 upper", "Audio.Callback maximum", "Audio.Callback over half period total",
            "Audio.Completion drops total", "Audio.Output mode" };
        static constexpr std::array units{
            "voices", "voices", "voices", "voices", "voices", "voices", "count", "instances",
            "us", "B", "count", "us", "us", "count", "count", "mode" };
        static const auto ids = []
        {
            std::array<ce::profile_counter_id, names.size()> result{};
            for (std::size_t index = 0u; index < names.size(); ++index)
            {
                result[index] = ce::register_counter(names[index], units[index], ce::counter_category::audio);
            }
            return result;
        }();
        const auto counters = host.Counters();
        const std::array values{
            static_cast<double>(counters.voices.active), static_cast<double>(counters.voices.physical),
            static_cast<double>(counters.voices.virtualized), static_cast<double>(counters.voices.paused),
            static_cast<double>(counters.voices.stolen), static_cast<double>(counters.voices.dropped),
            static_cast<double>(counters.voices.backendFailures), static_cast<double>(playback.AliveCount()),
            static_cast<double>(counters.runtimeUpdateNanoseconds) / 1000.0,
            static_cast<double>(counters.streamBytesRead), static_cast<double>(counters.streamReadFailures),
            static_cast<double>(counters.callbackP99Nanoseconds) / 1000.0,
            static_cast<double>(counters.callbackMaxNanoseconds) / 1000.0,
            static_cast<double>(counters.callbackOverHalfPeriod), static_cast<double>(playback.DroppedCompletions()),
            static_cast<double>(host.Mode()) };
        std::array<ce::profile_counter_sample, names.size()> samples{};
        std::size_t count = 0u;
        for (std::size_t index = 0u; index < values.size(); ++index)
        {
            if ((index == 9u || index == 10u) && !counters.backendCountersAvailable)
            {
                continue;
            }
            if (index >= 11u && index <= 13u && counters.callbackCount == 0u)
            {
                continue;
            }
            samples[count++] = { ids[index], values[index] };
        }
        profiler.publish_counters(profiler.current_frame(), ce::counter_category::audio,
            std::span<const ce::profile_counter_sample>(samples.data(), count));
#else
        (void)host;
        (void)playback;
#endif
    }
}
