#pragma once
#include <array>
#include <filesystem>
#include <Windows.h>
#include <iostream>
#include "ClassProperty.h"
#include "EnginePaths.h"

namespace file = std::filesystem;

class InternalPath : public Singleton<InternalPath>
{
private:
	friend class Singleton<InternalPath>;
	InternalPath() = default;
	~InternalPath() = default;
public:
    file::path ExecuteablePath{};
    file::path DataPath{};
	file::path IconPath{};
    file::path ShaderSourcePath{};
	file::path ModelSourcePath{};
	file::path TextureSourcePath{};
	file::path UISourcePath{};
	file::path PrefabSourcePath{};
	file::path MaterialSourcePath{};
	file::path PrecompiledShaderPath{};
    file::path DynamicSolutionDir{};
	file::path BaseProjectPath{};
	file::path RuntimeContentRoot{};
	file::path RuntimeDataRoot{};
	file::path ManagedRoot{};
	file::path EngineResourceRoot{};
	file::path CacheRoot{};
	file::path ConfigRoot{};
	file::path TraceRoot{};
	file::path TestArtifactRoot{};
	file::path ProjectSettingsPath{};
	file::path TerrainSourcePath{};
	file::path DumpPath{};
	file::path LogPath{};
	file::path NodeEditorPath{};
	file::path volumeProfilePath{};
	file::path InputMapPath{};
	file::path animatorPath{};
	bool AssetAuthoringEnabled{ false };

    inline bool Initialize(const EnginePaths& paths)
    {
		if (!paths.IsValid()) return false;

		ExecuteablePath = paths.executableRoot.lexically_normal();
		RuntimeContentRoot = paths.runtimeContentRoot.lexically_normal();
		RuntimeDataRoot = paths.runtimeDataRoot.lexically_normal();
		ManagedRoot = (paths.managedRoot.empty()
			? ExecuteablePath / L"Managed"
			: paths.managedRoot).lexically_normal();
		EngineResourceRoot = (paths.engineResourceRoot.empty()
			? ExecuteablePath / L"Resources"
			: paths.engineResourceRoot).lexically_normal();
		CacheRoot = (RuntimeDataRoot / L"Cache").lexically_normal();
		ConfigRoot = (RuntimeDataRoot / L"Config").lexically_normal();
		TraceRoot = (RuntimeDataRoot / L"Traces").lexically_normal();
		TestArtifactRoot = (paths.testArtifactRoot.empty()
			? RuntimeDataRoot / L"Tests"
			: paths.testArtifactRoot).lexically_normal();
		DumpPath = (RuntimeDataRoot / "Dump").lexically_normal();
		LogPath = (RuntimeDataRoot / "Log").lexically_normal();
		BaseProjectPath = paths.projectRoot.lexically_normal();
		AssetAuthoringEnabled = paths.enableAssetAuthoring;

		// Host-owned mutable data is valid in both Editor and packaged Player. It is
		// always prepared independently from the asset-authoring capability.
		for (const file::path& runtimeDirectory : {
			LogPath, DumpPath, CacheRoot, ConfigRoot, TraceRoot, TestArtifactRoot })
		{
			std::error_code runtimeDirError{};
			file::create_directories(runtimeDirectory, runtimeDirError);
			if (runtimeDirError ||
				!file::is_directory(runtimeDirectory, runtimeDirError) || runtimeDirError)
			{
				std::cerr << "Failed to prepare runtime data directory: "
					<< runtimeDirectory.string() << '\n';
				return false;
			}
		}

		// The process host resolves project/package roots before runtime initialization.
		// PathFinder derives only stable subdirectories and never branches on host identity.
		const file::path runtimeContentRoot = RuntimeContentRoot;
		const file::path assetsRoot = paths.assetsRoot.lexically_normal();

		DataPath = assetsRoot;
		ModelSourcePath = assetsRoot / "Models";
		TextureSourcePath = assetsRoot / "Textures";
		MaterialSourcePath = assetsRoot / "Materials";
		UISourcePath = assetsRoot / "UI";
		PrefabSourcePath = assetsRoot / "Prefabs";
		ShaderSourcePath = assetsRoot / "Shaders";
		DynamicSolutionDir = runtimeContentRoot;
		ProjectSettingsPath =
			(runtimeContentRoot / L"ProjectSetting").lexically_normal();

		PrecompiledShaderPath = (EngineResourceRoot / L"Shaders").lexically_normal();
		IconPath = (EngineResourceRoot / L"Icons").lexically_normal();
		TerrainSourcePath = assetsRoot / "Terrain";
		NodeEditorPath = assetsRoot / "NodeEditor";
		volumeProfilePath = assetsRoot / "VolumeProfile";
		InputMapPath = assetsRoot / "InputMap";
		// GameBuildSlnPath가 여기 있었다 — 게임 빌드가 MSBuild를 부르지
		// 않게 되면서(B0-3, BuildPipelinePlan §2.0) 소비자와 함께 걷었다.
		animatorPath = assetsRoot / "AnimatorController";

		DataPath = DataPath.lexically_normal();
		ModelSourcePath = ModelSourcePath.lexically_normal();
		TextureSourcePath = TextureSourcePath.lexically_normal();
		MaterialSourcePath = MaterialSourcePath.lexically_normal();
		UISourcePath = UISourcePath.lexically_normal();
		PrefabSourcePath = PrefabSourcePath.lexically_normal();
		ShaderSourcePath = ShaderSourcePath.lexically_normal();
		TerrainSourcePath = TerrainSourcePath.lexically_normal();
		NodeEditorPath = NodeEditorPath.lexically_normal();
		volumeProfilePath = volumeProfilePath.lexically_normal();
		InputMapPath = InputMapPath.lexically_normal();
		animatorPath = animatorPath.lexically_normal();
		//dir not exist -> create dir

		std::vector<file::path> directories = {
			DataPath,
			ShaderSourcePath,
			ModelSourcePath,
			TextureSourcePath,
			MaterialSourcePath,
			UISourcePath,
			PrefabSourcePath,
			IconPath,
			DynamicSolutionDir,
			PrecompiledShaderPath,
			ProjectSettingsPath,
			TerrainSourcePath,
			volumeProfilePath,
			NodeEditorPath,
			InputMapPath,
			animatorPath,
		};

		// Authoring directory creation is an explicit Host capability. Packaged content
		// roots are populated by extraction and must not gain empty source directories.
		if (paths.enableAssetAuthoring)
		{
			for (const auto& path : directories)
			{
				if (!file::exists(path))
				{
					file::create_directories(path);
				}
			}
		}
		return true;
    }
};

class PathFinder
{
public:
	static inline bool Initialize(const EnginePaths& paths) noexcept
    {
		return InternalPath::GetInstance()->Initialize(paths);
    }

	static inline file::path Relative()
	{
		return InternalPath::GetInstance()->DataPath;
	}

	static inline bool IsAssetAuthoringEnabled() noexcept
	{
		return InternalPath::GetInstance()->AssetAuthoringEnabled;
	}

	static inline file::path DumpPath()
	{
		return InternalPath::GetInstance()->DumpPath;
	}

	static inline file::path LogPath()
	{
		return InternalPath::GetInstance()->LogPath;
	}

	static inline file::path Relative(std::string_view path)
    {
        return file::path(InternalPath::GetInstance()->DataPath) / path;
    }

	static inline file::path RelativeToShader()
	{
		return file::path(InternalPath::GetInstance()->ShaderSourcePath);
	}

	static inline file::path RelativeToShader(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->ShaderSourcePath) / path;
	}

	static inline file::path RelativeToPrecompiledShader()
	{
		return file::path(InternalPath::GetInstance()->PrecompiledShaderPath);
	}

    static inline file::path RelativeToExecutable(std::string_view path)
    {
        return file::path(InternalPath::GetInstance()->ExecuteablePath) / path;
    }

	static inline file::path RelativeToMaterial(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->MaterialSourcePath) / path;
	}

    static inline file::path ShaderPath()
    {
        return InternalPath::GetInstance()->ShaderSourcePath;
    }

	static inline file::path IconPath()
	{
		return InternalPath::GetInstance()->IconPath;
	}

	static inline file::path ModelSourcePath()
	{
		return InternalPath::GetInstance()->ModelSourcePath;
	}

	static inline file::path TextureSourcePath()
	{
		return InternalPath::GetInstance()->TextureSourcePath;
	}

	static inline file::path UISourcePath()
	{
		return InternalPath::GetInstance()->UISourcePath;
	}

	static inline file::path PrefabSourcePath()
	{
		return InternalPath::GetInstance()->PrefabSourcePath;
	}

	static inline file::path MaterialSourcePath()
	{
		return InternalPath::GetInstance()->MaterialSourcePath;
	}

	static inline file::path BaseProjectPath()
	{
		return InternalPath::GetInstance()->BaseProjectPath;
	}

	static inline file::path RuntimeContentPath()
	{
		return InternalPath::GetInstance()->RuntimeContentRoot;
	}

	static inline file::path RuntimeDataPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->RuntimeDataRoot) / path;
	}

	static inline file::path CachePath(std::string_view path = {})
	{
		return file::path(InternalPath::GetInstance()->CacheRoot) / path;
	}

	static inline file::path ConfigPath(std::string_view path = {})
	{
		return file::path(InternalPath::GetInstance()->ConfigRoot) / path;
	}

	static inline file::path TracePath(std::string_view path = {})
	{
		return file::path(InternalPath::GetInstance()->TraceRoot) / path;
	}

	static inline file::path TestArtifactPath(std::string_view path = {})
	{
		return file::path(InternalPath::GetInstance()->TestArtifactRoot) / path;
	}

	static inline file::path ManagedPath(std::string_view path = {})
	{
		return file::path(InternalPath::GetInstance()->ManagedRoot) / path;
	}

	static inline file::path VolumeProfilePath()
	{
		return InternalPath::GetInstance()->volumeProfilePath;
	}

	static inline file::path DynamicSolutionPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->DynamicSolutionDir) / path;
	}

	static inline file::path ProjectSettingPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->ProjectSettingsPath) / path;
	}

	static inline file::path TerrainSourcePath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->TerrainSourcePath) / path;
	}

	static inline file::path NodeEditorPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->NodeEditorPath) / path;
	}

	static inline file::path RelativeToModel(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->ModelSourcePath) / path;
	}

	static inline file::path RelativeToTexture(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->TextureSourcePath) / path;
	}

	static inline file::path RelativeToUISource(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->UISourcePath) / path;
	}

	static inline file::path RelativeToPrefab(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->PrefabSourcePath) / path;
	}

	static inline file::path RelativeToBaseProject(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->BaseProjectPath) / path;
	}

	static inline file::path RelativeToVolumeProfile(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->volumeProfilePath) / path;
	}
	static inline file::path InputMapPath()
	{
		return InternalPath::GetInstance()->InputMapPath;
	}
	static inline file::path InputMapPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->InputMapPath) / path;
	}
	static inline file::path AnimatorjsonPath()
	{
		return InternalPath::GetInstance()->animatorPath;
	}
	static inline file::path AnimatorjsonPath(std::string_view path)
	{
		return file::path(InternalPath::GetInstance()->animatorPath) / path;
	}
};
