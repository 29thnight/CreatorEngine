#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "VolumeProfile.h"
#include "IRegistableEvent.h"
#include "VolumeComponent.generated.h"

class VolumeComponent : public Component, public RegistableEvent<VolumeComponent>
{
public:
    ReflectVolumeComponent
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(VolumeComponent)

    void Awake() override;
    void OnDestroy() override;

	void LoadProfile(FileGuid profileGuid);
    VolumeProfile& GetVolumeProfile() { return m_profile; }

    void UpdateProfileEditMode();
	bool IsProfileLoaded() const { return m_isProfileLoaded; }

    [[Property]]
	std::string m_volumeProfileName{};
    [[Property]]
    FileGuid m_volumeProfileGuid{ nullFileGuid };

private:
    RenderPassSettings m_prevSettings{};
    VolumeProfile m_profile{};
    bool m_isProfileLoaded{ false };
};
