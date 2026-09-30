#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "SceneRenderProfile.h"

class [[reflgen::reflect]] SceneRenderProfileComponent : public meta::identity<SceneRenderProfileComponent, Component>
{
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
    [[reflgen::ignore]]
    RenderPassSettings m_prevSettings{};

    [[reflgen::ignore]]
    SceneRenderProfile m_profile{};

    [[reflgen::ignore]]
    bool m_isProfileLoaded{false};
};
