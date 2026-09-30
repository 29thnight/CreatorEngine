#include "SceneRenderProfileComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "RuntimeSettings.h"
#include "AuthoringParsedDocument.h"

void SceneRenderProfileComponent::OnInitialized()
{
    if (!m_isProfileLoaded)
    {
        m_prevSettings = RuntimeSettings::Get().GetRenderPassSettings();

        if (m_renderProfileGuid == nullFileGuid)
            return;

        file::path path = DataSystems->GetFilePath(m_renderProfileGuid);
        if (!path.empty() && file::exists(path))
        {
            std::string parseError;
            const Authoring::ParsedDocument document = Authoring::ParsedDocument::ParseFile(path.string(), parseError);
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
}

void SceneRenderProfileComponent::OnUninitializing()
{
    if (m_isProfileLoaded)
    {
        RuntimeSettings::Get().SetRenderPassSettings(m_prevSettings);

        SceneManagers->RequestRenderProfileApply();
    }
}

void SceneRenderProfileComponent::LoadProfile(FileGuid profileGuid)
{
    if (profileGuid == nullFileGuid)
        return;
    m_renderProfileGuid = profileGuid;
    file::path path = DataSystems->GetFilePath(m_renderProfileGuid);
    if (!path.empty() && file::exists(path))
    {
        std::string parseError;
        const Authoring::ParsedDocument document = Authoring::ParsedDocument::ParseFile(path.string(), parseError);
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
}

void SceneRenderProfileComponent::UpdateProfileEditMode()
{
    RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);
    SceneManagers->RequestRenderProfileApply();
}
