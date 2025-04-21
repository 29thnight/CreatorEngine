#pragma once
#include <efsw/efsw.hpp>
#include "AssetMetaRegistry.h"
#include <regex>
#include <yaml-cpp/yaml.h>

class AssetMetaWatcher : public efsw::FileWatchListener 
{
public:
    explicit AssetMetaWatcher(AssetMetaRegistry* registry) : 
        m_assetMetaRegistry(registry) {}

public:
    void handleFileAction(efsw::WatchID watchid,
        const std::string& dir,
        const std::string& filename,
        efsw::Action action,
        std::string oldFilename) override 
    {
        namespace fs = std::filesystem;

        fs::path dirPath(dir);
        fs::path filePath = dirPath / filename;

        switch (action) 
        {
        case efsw::Actions::Add:
            HandleCreated(filePath);
            break;
        case efsw::Actions::Moved:
            HandleMoved(dirPath, oldFilename, filename);
            break;
        case efsw::Actions::Delete:
            HandleDeleted(filePath);
            break;
        default:
            break;
        }
    }

    void ScanAndGenerateMissingMeta(const std::filesystem::path& root)
    {
        namespace fs = std::filesystem;

        for (auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;

            const fs::path& filePath = entry.path();
            if (!IsTargetFile(filePath))
                continue;

            fs::path metaPath = filePath.string() + ".meta";
            if (!fs::exists(metaPath))
            {
                CreateYamlMeta(filePath);
                std::cout << "[Meta Created on Startup] " << metaPath << std::endl;

				FileGuid guid = LoadGuidFromMeta(metaPath);
				m_assetMetaRegistry->Register(guid, filePath);
			}
			else
			{
				FileGuid guid = LoadGuidFromMeta(metaPath);
				if (!m_assetMetaRegistry->Contains(guid))
				{
					m_assetMetaRegistry->Register(guid, filePath);
				}
				else
				{
					std::cout << "[Meta Already Registered] " << metaPath << std::endl;
				}
            }
        }
    }

private:
    std::vector<std::string> ExtractFunctionNames(const std::filesystem::path& file)
    {
        std::vector<std::string> functions;
        std::ifstream fin(file);
        std::string line;

        std::regex funcRegex(
            R"((?:[\w:&<>\*\s]+)\s+[\w:]+::([\w_]+)\s*\([^)]*\)\s*\{?)"
        );

        while (std::getline(fin, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, funcRegex))
            {
                if (match.size() >= 2)
                {
                    std::string funcName = match[1].str();
                    functions.push_back(funcName);
                }
            }
        }
        return functions;
    }

    FileGuid LoadGuidFromMeta(const std::filesystem::path& metaPath)
    {
        YAML::Node node = YAML::LoadFile(metaPath.string());
        if (node["guid"])
            return FileGuid(node["guid"].as<std::string>());
        return {};
    }

    std::filesystem::path RemoveMetaExtension(const std::filesystem::path& metaPath)
    {
        auto stem = metaPath.stem();
        auto parent = metaPath.parent_path();

        return parent / stem;
    }

    bool IsTargetFile(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        return  extension == ".fbx" || extension == ".gltf" || extension == ".obj" || 
                extension == ".png" || extension == ".dds" || extension == ".hdr" ||
                extension == ".hlsl" || extension == ".cpp" || extension == ".glb" ||
                extension == ".cs" || extension == ".wav" || extension == ".mp3";
    }

    void HandleMoved(const std::filesystem::path& dir, const std::string& oldName, const std::string& newName) 
    {
        namespace fs = std::filesystem;

        fs::path oldPath = dir / oldName;
        fs::path newPath = dir / newName;
        fs::path oldMeta = oldPath.string() + ".meta";
        fs::path newMeta = newPath.string() + ".meta";
        try
        {
            if (fs::exists(oldMeta)) 
            {

                    fs::rename(oldMeta, newMeta);
                    std::cout << "[Meta Moved] " << oldMeta << " -> " << newMeta << std::endl;

            }
            else if (!fs::exists(newMeta)) 
            {
                CreateYamlMeta(newPath);
                std::cout << "[Meta Created] " << newMeta << std::endl;
            }

        }
        catch (const std::exception& e)
        {
            std::cerr << "Error moving .meta file: " << e.what() << std::endl;
        }
    }

    void HandleDeleted(const std::filesystem::path& deletedFile) 
    {
        namespace fs = std::filesystem;

        fs::path metaFile = deletedFile.string() + ".meta";
        if (fs::exists(metaFile)) 
        {
            try 
            {
				if (deletedFile.extension() == ".meta")
				{
					FileGuid guid = LoadGuidFromMeta(deletedFile);
					m_assetMetaRegistry->Unregister(guid);
					m_assetMetaRegistry->Unregister(deletedFile);
				}

                fs::remove(metaFile);
                std::cout << "[Meta Deleted] " << metaFile << std::endl;
            }
            catch (const std::exception& e) 
            {
                std::cerr << "Error deleting .meta file: " << e.what() << std::endl;
            }
        }
    }

    void HandleCreated(const std::filesystem::path& filePath)
    {
        if (!IsTargetFile(filePath))
            return;

        std::filesystem::path metaPath = filePath.string() + ".meta";
        if (!std::filesystem::exists(metaPath))
        {
            CreateYamlMeta(filePath);
            std::cout << "[Meta Created] " << metaPath << std::endl;
        }

        if (filePath.extension() == ".meta")
        {
            FileGuid guid = LoadGuidFromMeta(filePath);
            std::filesystem::path targetFile = RemoveMetaExtension(filePath);
            m_assetMetaRegistry->Register(guid, targetFile);
        }
    }

    void CreateYamlMeta(const std::filesystem::path& targetFile) 
    {
        std::filesystem::path metaPath = targetFile.string() + ".meta";

        using namespace TypeTrait;
        YAML::Node root;
        root["guid"] = GUIDCreator::MakeFileGUID().ToString();
        root["importSettings"]["extension"] = targetFile.extension().string();
        root["importSettings"]["timestamp"] = std::filesystem::last_write_time(targetFile).time_since_epoch().count();

        if (targetFile.extension() == ".cpp")
        {
            auto functions = ExtractFunctionNames(targetFile);
            for (const auto& f : functions)
                root["eventRegisterSetting"].push_back(f);
        }

        std::ofstream fout(metaPath);
        fout << root;
    }

private:
    AssetMetaRegistry* m_assetMetaRegistry;
};