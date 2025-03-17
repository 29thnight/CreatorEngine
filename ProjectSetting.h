#pragma once
#include "Core.Minimal.h"

class ProjectSetting
{
public:
	ProjectSetting() = default;
	~ProjectSetting() = default;
	void Initialize();
	void Finalize();
	void Load();
	void Save();
	void Show();
	void SetProjectPath(const std::wstring_view& path);
	file::path GetProjectPath() const;

private:
	file::path m_projectPath;
};