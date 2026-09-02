#include "VolumeComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "RuntimeSettings.h"
#include "AuthoringParsedDocument.h"

void VolumeComponent::OnInitialized()
{
    if(!m_isProfileLoaded)
    {
        m_prevSettings = RuntimeSettings::Get().GetRenderPassSettings();

        if (m_volumeProfileGuid == nullFileGuid)
            return;

        file::path path = DataSystems->GetFilePath(m_volumeProfileGuid);
        if (!path.empty() && file::exists(path))
        {
            std::string parseError;
            const Authoring::ParsedDocument document =
                Authoring::ParsedDocument::ParseFile(path.string(), parseError);
            const Authoring::ReadNode node = document.Root();
            if (!document)
            {
                Debug->LogError("Volume profile parse failed: " + parseError);
            }
            else if (node["settings"])
            {
                Meta::Deserialize(&m_profile.settings, node["settings"]);
                RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);

                m_isProfileLoaded = true;
            }
        }

        SceneManagers->VolumeProfileApply();
    }
}

void VolumeComponent::OnUninitializing()
{
    if(m_isProfileLoaded)
    {
        RuntimeSettings::Get().SetRenderPassSettings(m_prevSettings);

        SceneManagers->VolumeProfileApply();
    }
}

void VolumeComponent::LoadProfile(FileGuid profileGuid)
{
    if (profileGuid == nullFileGuid)
        return;
    m_volumeProfileGuid = profileGuid;
    file::path path = DataSystems->GetFilePath(m_volumeProfileGuid);
    if (!path.empty() && file::exists(path))
    {
        std::string parseError;
        const Authoring::ParsedDocument document =
            Authoring::ParsedDocument::ParseFile(path.string(), parseError);
        const Authoring::ReadNode node = document.Root();
        if (!document)
        {
            Debug->LogError("Volume profile parse failed: " + parseError);
        }
        else if (node["settings"])
        {
            Meta::Deserialize(&m_profile.settings, node["settings"]);
			m_prevSettings = RuntimeSettings::Get().GetRenderPassSettings();
            RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);

            m_isProfileLoaded = true;
        }
    }
	SceneManagers->VolumeProfileApply();
}

void VolumeComponent::UpdateProfileEditMode()
{
    RuntimeSettings::Get().SetRenderPassSettings(m_profile.settings);
    SceneManagers->VolumeProfileApply();
}
