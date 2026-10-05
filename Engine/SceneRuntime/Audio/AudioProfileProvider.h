#pragma once

namespace wave
{
    class AudioHost;
    class PlaybackService;

    // Called by the host/game thread after audio update. Never a callback hook.
    void PublishAudioProfile(const AudioHost& host, const PlaybackService& playback);
}
