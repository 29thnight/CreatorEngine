#include "VolumeComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "RuntimeSettings.h"

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
            MetaYml::Node node = MetaYml::LoadFile(path.string());
            if (node["settings"])
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
        MetaYml::Node node = MetaYml::LoadFile(path.string());
        if (node["settings"])
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
