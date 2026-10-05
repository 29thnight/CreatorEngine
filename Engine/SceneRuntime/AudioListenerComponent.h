#pragma once
#include "Component.h"
#include "Audio/ListenerState.h"
#include <mutex>
#include <optional>

// Listening is independent of rendering cameras. Enable one active listener per world.
class [[reflgen::reflect]] AudioListenerComponent
    : public meta::identity<AudioListenerComponent, Component>
{
public:
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;

    [[nodiscard]] wave::ListenerState CaptureListener() const;
    struct Settings
    {
        bool active{ true };
        math::vector3 velocity{};
    };
    [[nodiscard]] Settings ReadSettings() const;
    void QueueSettings(Settings settings);
    void ApplyPendingSettings();
    void SetActive(bool value);

    bool active{ true };
    math::vector3 velocity{ 0.0f, 0.0f, 0.0f };
private:
    [[reflgen::ignore]]
    mutable std::mutex m_settingsMutex;

    [[reflgen::ignore]]
    std::optional<Settings> m_pending;
};
