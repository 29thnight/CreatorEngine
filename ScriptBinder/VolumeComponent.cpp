#include "VolumeComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "EngineSetting.h"
#include "MetaYaml.h"

void VolumeComponent::Awake()
{
    if(!m_isProfileLoaded)
    {
        m_prevSettings = EngineSettingInstance->GetRenderPassSettings();

        if (m_volumeProfileGuid == nullFileGuid)
            return;

        file::path path = DataSystems->GetFilePath(m_volumeProfileGuid);
        if (!path.empty() && file::exists(path))
        {
            MetaYml::Node node = MetaYml::LoadFile(path.string());
            if (node["settings"])
            {
                Meta::Deserialize(&m_profile.settings, node["settings"]);
                EngineSettingInstance->GetRenderPassSettingsRW() = m_profile.settings;

                m_isProfileLoaded = true;
            }
        }

        SceneManagers->VolumeProfileApply();
    }
}

void VolumeComponent::OnDestroy()
{
    if(m_isProfileLoaded)
    {
        EngineSettingInstance->SetRenderPassSettings(m_prevSettings);

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
			m_prevSettings = EngineSettingInstance->GetRenderPassSettings();
            EngineSettingInstance->GetRenderPassSettingsRW() = m_profile.settings;

            m_isProfileLoaded = true;
        }
    }
	SceneManagers->VolumeProfileApply();
}

void VolumeComponent::UpdateProfileEditMode()
{
    EngineSettingInstance->GetRenderPassSettingsRW() = m_profile.settings;
    SceneManagers->VolumeProfileApply();
}
