#pragma once

#include "../Utility_Framework/Foundation/StandardLibrary.h"

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
// Native routes own exclusion through baseline, positive and control captures;
// SDK routes observe actual non-reset SDK submissions without native exclusion.
// Complete means artifacts were recorded, never acceptance PASS.
struct EnhancedLiveViewRequest;

class TemporalMotionFixture
{
public:
    // busy reports transient renderer-lock contention, not another active capture.
    // sdkStartup reduces the observed baseline minimum from 32 to 1; it does
    // not capture the reset submission or guarantee the first frame of appearance.
    static bool Begin(const std::string& directory, std::string& error, bool* busy = nullptr, bool sdkQuality = false, bool sdkStartup = false, bool sdkRoutes = false, bool companionView = true);
    static void TickAfterAnimation();
    static bool CaptureCompanionView(EnhancedLiveViewRequest& view);
    // The Editor must exclude presentation-side scene readers while ticking an
    // active fixture: preparation and cleanup change the entity hierarchy.
    // Game-thread query; includes pending cleanup, and does not copy status text.
    static bool RequiresSceneWriteBoundary() noexcept;
    static void Cancel(const std::string& reason);
    // Host calls only after presentation/render joins, before native/GC teardown.
    static void ShutdownAfterRenderJoin() noexcept;
    static TemporalMotionFixtureStatus GetStatus();
};
