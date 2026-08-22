#pragma once
#include "Core.Minimal.h"
#include <mutex>
#include <shared_mutex>

class AssetMetaRegistry
{
public:
    void Register(const FileGuid& guid, const file::path& path)
    {
		std::unique_lock lock(m_mutex);
		if (const auto pathIt = m_pathToGuid.find(path);
			pathIt != m_pathToGuid.end() && pathIt->second != guid)
		{
			m_guidToPath.erase(pathIt->second);
		}
		if (const auto guidIt = m_guidToPath.find(guid);
			guidIt != m_guidToPath.end() && guidIt->second != path)
		{
			m_pathToGuid.erase(guidIt->second);
		}
        m_guidToPath[guid] = path;
        m_pathToGuid[path] = guid;
    }

    void Unregister(const FileGuid& guid)
    {
		std::unique_lock lock(m_mutex);
        auto it = m_guidToPath.find(guid);
        if (it != m_guidToPath.end())
        {
            file::path path = it->second;
            m_guidToPath.erase(it);
            m_pathToGuid.erase(path);
        }
    }

    void Unregister(const file::path& path)
    {
		std::unique_lock lock(m_mutex);
        auto it = m_pathToGuid.find(path);
        if (it != m_pathToGuid.end())
        {
            FileGuid guid = it->second;
            m_pathToGuid.erase(it);
            m_guidToPath.erase(guid);
        }
    }

    file::path GetPath(const FileGuid& guid) const
    {
		std::shared_lock lock(m_mutex);
        auto it = m_guidToPath.find(guid);
        return it != m_guidToPath.end() ? it->second : file::path{};
    }

    FileGuid GetGuid(const file::path& path) const
    {
		std::shared_lock lock(m_mutex);
        auto it = m_pathToGuid.find(path);
        return it != m_pathToGuid.end() ? it->second : FileGuid{};
    }

    FileGuid GetFilenameToGuid(const std::string& filename) const
    {
		std::shared_lock lock(m_mutex);
		auto it = std::find_if(m_pathToGuid.begin(), m_pathToGuid.end(),
			[&filename](const auto& pair) { return pair.first.filename() == filename; });
		return it != m_pathToGuid.end() ? it->second : FileGuid{};
    }

	FileGuid GetStemToGuid(const std::string& stem) const
	{
		std::shared_lock lock(m_mutex);
		auto it = std::find_if(m_pathToGuid.begin(), m_pathToGuid.end(),
			[&stem](const auto& pair) { return pair.first.stem() == stem; });
		return it != m_pathToGuid.end() ? it->second : FileGuid{};
	}

    bool Contains(const FileGuid& guid) const
    {
		std::shared_lock lock(m_mutex);
        return m_guidToPath.contains(guid);
    }

    bool Contains(const file::path& path) const
    {
		std::shared_lock lock(m_mutex);
        return m_pathToGuid.contains(path);
    }

	void Clear()
	{
		std::unique_lock lock(m_mutex);
		m_guidToPath.clear();
		m_pathToGuid.clear();
	}

private:
	mutable std::shared_mutex m_mutex;
    std::unordered_map<FileGuid, file::path> m_guidToPath;
    std::unordered_map<file::path, FileGuid> m_pathToGuid;
};
