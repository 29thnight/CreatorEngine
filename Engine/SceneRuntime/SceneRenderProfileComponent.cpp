#include "SceneRenderProfileComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "../Utility_Framework/Runtime/RuntimeSettings.h"
#include "../Utility_Framework/Authoring/AuthoringParsedDocument.h"

void SceneRenderProfileComponent::OnInitialized()
{
#if defined(CE_SCENE_HEADLESS)
    return;
#else
    if (!m_isProfileLoaded)
    {
        m_prevSettings = RuntimeSettings::Get().GetRenderPassSettings();

        if (m_renderProfileGuid == nullFileGuid)
            return;

        std::filesystem::path path = DataSystems->GetFilePath(m_renderProfileGuid);
        if (!path.empty() && std::filesystem::exists(path))
        {
            std::string parseError;
            const Authoring::ParsedDocument document = Authoring::ParsedDocument::ParseFilePath(path, parseError);
            const Authoring::ReadNode node = document.Root();
            if (!document)
            {
                Debug::PrintLog(spdlog::level::err, "Scene render profile parse failed: " + parseError);
            }
            else if (node["settings"])
            {
                Meta::Deserialize(&m_profile.settings, node["settings"]);
                RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);

                m_isProfileLoaded = true;
            }
        }

        SceneManagers->RequestRenderProfileApply();
    }

#endif
}

void SceneRenderProfileComponent::OnUninitializing()
{
#if defined(CE_SCENE_HEADLESS)
    return;
#else
    if (m_isProfileLoaded)
    {
        RuntimeSettings::Get().SetRenderPassSettings(m_prevSettings);

        SceneManagers->RequestRenderProfileApply();
    }

#endif
}

void SceneRenderProfileComponent::LoadProfile(FileGuid profileGuid)
{
#if defined(CE_SCENE_HEADLESS)
    throw std::logic_error("Presentation service is not installed");
#else
    if (profileGuid == nullFileGuid)
        return;
    m_renderProfileGuid = profileGuid;
    std::filesystem::path path = DataSystems->GetFilePath(m_renderProfileGuid);
    if (!path.empty() && std::filesystem::exists(path))
    {
        std::string parseError;
        const Authoring::ParsedDocument document = Authoring::ParsedDocument::ParseFilePath(path, parseError);
        const Authoring::ReadNode node = document.Root();
        if (!document)
        {
            Debug::PrintLog(spdlog::level::err, "Scene render profile parse failed: " + parseError);
        }
        else if (node["settings"])
        {
            Meta::Deserialize(&m_profile.settings, node["settings"]);
            m_prevSettings = RuntimeSettings::Get().GetRenderPassSettings();
            RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);

            m_isProfileLoaded = true;
        }
    }
    SceneManagers->RequestRenderProfileApply();

#endif
}

void SceneRenderProfileComponent::UpdateProfileEditMode()
{
#if defined(CE_SCENE_HEADLESS)
    throw std::logic_error("Presentation service is not installed");
#else
    RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);
    SceneManagers->RequestRenderProfileApply();

#endif
}
