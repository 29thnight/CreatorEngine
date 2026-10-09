#pragma once

#include <cstdint>
#include <string>

enum class TemporalMotionFixtureState : std::uint8_t
{
    Idle, Preparing, Running, Complete, Failed,
};

struct TemporalMotionFixtureStatus
{
    TemporalMotionFixtureState state{ TemporalMotionFixtureState::Idle };
    std::string directory;
    std::string route;
    std::string error;
};

// Explicit development diagnostic. All entry points are game-thread only.
// The session owns native exclusion through baseline, positive and control
// captures. Complete means artifacts were recorded, never acceptance PASS.
class TemporalMotionFixture
{
public:
    static bool Begin(const std::string& directory, std::string& error);
    static void TickAfterAnimation();
    static void Cancel(const std::string& reason);
    // Host calls only after presentation/render joins, before native/GC teardown.
    static void ShutdownAfterRenderJoin() noexcept;
    static TemporalMotionFixtureStatus GetStatus();
};
