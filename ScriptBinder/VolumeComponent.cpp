#include "VolumeComponent.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "EngineSetting.h"

void VolumeComponent::Awake()
{
    m_prevSettings = EngineSettingInstance.GetRenderPassSettings();

    if (m_volumeProfileGuid == nullFileGuid)
        return;

    file::path path = DataSystems->GetFilePath(m_volumeProfileGuid);
    if (!path.empty() && file::exists(path))
    {
        MetaYml::Node node = MetaYml::LoadFile(path.string());
        if (node["settings"])
        {
            Meta::Deserialize(&m_profile.settings, node["settings"]);
            EngineSettingInstance.GetRenderPassSettings() = m_profile.settings;
        }
    }
}

void VolumeComponent::OnDestroy()
{
    EngineSettingInstance.GetRenderPassSettings() = m_prevSettings;
}
