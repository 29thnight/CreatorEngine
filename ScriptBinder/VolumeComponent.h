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

    [[Property]]
    FileGuid m_volumeProfileGuid{ nullFileGuid };

private:
    RenderPassSettings m_prevSettings{};
    VolumeProfile m_profile{};
};
