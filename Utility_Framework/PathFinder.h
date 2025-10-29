#pragma once
#include <filesystem>
#include <Windows.h>
#include <iostream>
#include "DLLAcrossSingleton.h"

inline constexpr const char* VSWHERE_PATH = R"(C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe)";

inline std::string ExecuteVsWhere()
{
	std::string result;
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
	HANDLE hReadPipe, hWritePipe;

	if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
	{
		std::cerr << "Failed to create pipe\n";
		return "";
	}

	PROCESS_INFORMATION pi{};
	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.hStdOutput = hWritePipe;
	si.hStdError = hWritePipe;
	si.dwFlags |= STARTF_USESTDHANDLES;

	std::string cmd = std::string("\"") + VSWHERE_PATH + "\" -prerelease -latest -products * -property installationPath";

	if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		std::cerr << "Failed to execute vswhere\n";
		CloseHandle(hReadPipe);
		CloseHandle(hWritePipe);
		return "";
	}

	CloseHandle(hWritePipe); // parent doesn't write

	char buffer[512];
	DWORD bytesRead;
	while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr))
	{
		buffer[bytesRead] = '\0';
		result += buffer;
	}

	CloseHandle(hReadPipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return result;
}

namespace file = std::filesystem;

class InternalPath : public DLLCore::Singleton<InternalPath>
{
private:
	friend class DLLCore::Singleton<InternalPath>;
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
	std::wstring MsbuildPreviewExe = L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Preview\\MSBuild\\Current\\Bin\\MSBuild.exe";
	std::wstring MsbuildExe = L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";
	std::wstring MSBuild18var = L"C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\MSBuild\\Current\\Bin\\MSBuild.exe";
    file::path DynamicSolutionDir{};
	file::path BaseProjectPath{};
	file::path ProjectSettingsPath{};
	file::path TerrainSourcePath{};
	file::path DumpPath{};
	file::path NodeEditorPath{};
	file::path volumeProfilePath{};
	file::path InputMapPath{};
	file::path GameBuildSlnPath{};
	file::path animatorPath{};

    inline void Initialize()
    {
        HMODULE hModule = GetModuleHandleW(NULL);
        WCHAR path[MAX_PATH]{};

        GetModuleFileNameW(hModule, path, MAX_PATH);
        file::path p(path);

        ExecuteablePath = p.remove_filename();

        auto base = file::path(ExecuteablePath);
		//TODO 지금은 이런식으로 불러오고 나중에는 기본 ini 설정값을 정해서 읽어오는 걸로 합시다.
		DumpPath = file::path(base).append("Dump\\").lexically_normal();
		BaseProjectPath = file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();

#ifdef BUILD_FLAG
		std::array<WCHAR, MAX_PATH> tempPathBuffer{};
		const DWORD tempPathLength = GetTempPathW(static_cast<DWORD>(tempPathBuffer.size()), tempPathBuffer.data());

		file::path assetsRoot;
		if (tempPathLength > 0 && tempPathLength < tempPathBuffer.size())
		{
			assetsRoot = file::path(tempPathBuffer.data()) / L"UnpackedAssets" / L"Assets";
		}
		else
		{
			assetsRoot = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\");
		}

		assetsRoot = assetsRoot.lexically_normal();
#else
		file::path assetsRoot = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\").lexically_normal();
#endif

		DataPath = assetsRoot;
		ModelSourcePath = assetsRoot / "Models";
		TextureSourcePath = assetsRoot / "Textures";
		MaterialSourcePath = assetsRoot / "Materials";
		UISourcePath = assetsRoot / "UI";
		PrefabSourcePath = assetsRoot / "Prefabs";
		ShaderSourcePath = assetsRoot / "Shaders";
#ifdef BUILD_FLAG
		DynamicSolutionDir = file::path(tempPathBuffer.data()) / L"UnpackedAssets";
		ProjectSettingsPath = file::path(tempPathBuffer.data()) / L"UnpackedAssets" / L"ProjectSetting";
		DynamicSolutionDir = DynamicSolutionDir.lexically_normal();
		ProjectSettingsPath = ProjectSettingsPath.lexically_normal();
#else
		DynamicSolutionDir = file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
		ProjectSettingsPath = file::path(base).append("..\\..\\Dynamic_CPP\\ProjectSetting").lexically_normal();
#endif

		PrecompiledShaderPath = file::path(base).append("..\\Assets\\Shaders\\").lexically_normal();
		IconPath = file::path(base).append("..\\Icons\\").lexically_normal();
		TerrainSourcePath = assetsRoot / "Terrain";
		NodeEditorPath = assetsRoot / "NodeEditor";
		volumeProfilePath = assetsRoot / "VolumeProfile";
		InputMapPath = assetsRoot / "InputMap";
		GameBuildSlnPath = file::path(base).append("..\\..\\GameBuild.sln").lexically_normal();
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

		std::vector<file::path> paths = {
			DumpPath,
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

#ifndef BUILD_FLAG
		for (const auto& path : paths)
		{
			if (!file::exists(path))
			{
				file::create_directories(path);
			}
		}
#endif
    }
};

class PathFinder
{
public:
	static inline void Initialize() noexcept
    {
        InternalPath::GetInstance()->Initialize();
    }

	static inline file::path Relative()
	{
		return InternalPath::GetInstance()->DataPath;
	}

	static inline file::path DumpPath()
	{
		return InternalPath::GetInstance()->DumpPath;
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

	static inline std::wstring MsbuildPreviewPath()
	{
		return InternalPath::GetInstance()->MsbuildPreviewExe;
	}

	static inline std::wstring MSBuild18Path()
	{
		return InternalPath::GetInstance()->MSBuild18var;
	}

    static inline std::wstring MsbuildPath()
    {
        return InternalPath::GetInstance()->MsbuildExe;
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

	static inline file::path VolumeProfilePath()
	{
		return InternalPath::GetInstance()->volumeProfilePath;
	}

	static inline file::path GameBuildSlnPath()
	{
		return InternalPath::GetInstance()->GameBuildSlnPath;
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
