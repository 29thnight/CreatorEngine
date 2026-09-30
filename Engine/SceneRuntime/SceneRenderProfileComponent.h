#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "SceneRenderProfile.h"

class SceneRenderProfileComponent : public meta::identity<SceneRenderProfileComponent, Component>
{
  public:
    static consteval auto reflect()
    {
        return meta::schema<Self>(meta::field<&Self::m_renderProfileName>, meta::field<&Self::m_renderProfileGuid>);
    }

  public:
    SceneRenderProfileComponent() = default;

    void OnInitialized() override;
    void OnUninitializing() override;

    void LoadProfile(FileGuid profileGuid);
    SceneRenderProfile& GetRenderProfile() { return m_profile; }

    void UpdateProfileEditMode();
    bool IsProfileLoaded() const { return m_isProfileLoaded; }

    std::string m_renderProfileName{};
    FileGuid m_renderProfileGuid{nullFileGuid};

  private:
    RenderPassSettings m_prevSettings{};
    SceneRenderProfile m_profile{};
    bool m_isProfileLoaded{false};
};
