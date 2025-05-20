#pragma once
#include "Core.Minimal.h"

class ProjectSetting : public Singleton<ProjectSetting>
{
private:
	friend class Singleton;
	ProjectSetting() = default;
	~ProjectSetting() = default;

public:
	void Initialize();
	void Finalize();
	void Load();
	void Save();

public:
	std::wstring m_projectName;
};

static auto& ProjectSettingInstance = ProjectSetting::GetInstance();