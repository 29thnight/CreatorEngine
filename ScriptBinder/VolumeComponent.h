#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "VolumeProfile.h"

class VolumeComponent : public meta::identity<VolumeComponent, Component>
{
public:
    static consteval auto describe()
    {
        return meta::describe<VolumeComponent>(
            meta::base<Component>(),
            meta::member<&VolumeComponent::m_volumeProfileName>(),
            meta::member<&VolumeComponent::m_volumeProfileGuid>());
    }
    VolumeComponent() = default;

    void Awake() override;
    void OnDestroy() override;

	void LoadProfile(FileGuid profileGuid);
    VolumeProfile& GetVolumeProfile() { return m_profile; }

    void UpdateProfileEditMode();
	bool IsProfileLoaded() const { return m_isProfileLoaded; }

	std::string m_volumeProfileName{};
    FileGuid m_volumeProfileGuid{ nullFileGuid };

private:
    RenderPassSettings m_prevSettings{};
    VolumeProfile m_profile{};
    bool m_isProfileLoaded{ false };
};
