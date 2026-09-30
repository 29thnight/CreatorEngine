#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "VolumeProfile.h"

class [[reflgen::reflect]] VolumeComponent : public meta::identity<VolumeComponent, Component>
{
    public:
public:
    VolumeComponent() = default;

    void OnInitialized() override;
    void OnUninitializing() override;

	void LoadProfile(FileGuid profileGuid);
    VolumeProfile& GetVolumeProfile() { return m_profile; }

    void UpdateProfileEditMode();
	bool IsProfileLoaded() const { return m_isProfileLoaded; }

	std::string m_volumeProfileName{};
    FileGuid m_volumeProfileGuid{ nullFileGuid };

private:
    [[reflgen::ignore]]
    RenderPassSettings m_prevSettings{};

    [[reflgen::ignore]]
    VolumeProfile m_profile{};

    [[reflgen::ignore]]
    bool m_isProfileLoaded{ false };
};
