#pragma once
#include <filesystem>
#include <Windows.h>

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

	std::string cmd = std::string("\"") + VSWHERE_PATH + "\" -latest -products * -property installationPath";

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

namespace InternalPath
{
    inline file::path ExecuteablePath{};
    inline file::path DataPath{};
	inline file::path IconPath{};
    inline file::path ShaderSourcePath{};
	inline file::path ModelSourcePath{};
	inline file::path TextureSourcePath{};
	inline file::path UISourcePath{};
	inline file::path PrefabSourcePath{};
	inline file::path MaterialSourcePath{};
	inline file::path PrecompiledShaderPath{};
	inline std::wstring MsbuildPreviewExe = L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Preview\\MSBuild\\Current\\Bin\\MSBuild.exe";
	inline std::wstring MsbuildExe = L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe";
    inline file::path DynamicSolutionDir{};
	inline file::path BaseProjectPath{};
	inline file::path ProjectSettingsPath{};
	inline file::path TerrainSourcePath{};
	inline file::path DumpPath{};
	inline file::path NodeEditorPath{};
	inline file::path volumeProfilePath{};
    inline void Initialize()
    {
        HMODULE hModule = GetModuleHandleW(NULL);
        WCHAR path[MAX_PATH]{};

        GetModuleFileNameW(hModule, path, MAX_PATH);
        file::path p(path);

        ExecuteablePath = p.remove_filename();

        auto base = file::path(ExecuteablePath);
		//TODO 지금은 이런식으로 불러오고 나중에는 기본 ini 설정값을 정해서 읽어오는 걸로 합시다.
		DumpPath				= file::path(base).append("Dump\\").lexically_normal();
		BaseProjectPath			= file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
        DataPath				= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\").lexically_normal();
		ModelSourcePath			= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Models\\").lexically_normal();
		TextureSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Textures\\").lexically_normal();
		MaterialSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Materials\\").lexically_normal();
		UISourcePath			= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\UI\\").lexically_normal();
		PrefabSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Prefabs\\").lexically_normal();
		ShaderSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Shaders\\").lexically_normal();
		DynamicSolutionDir		= file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
		ProjectSettingsPath		= file::path(base).append("..\\..\\Dynamic_CPP\\ProjectSetting").lexically_normal();
		PrecompiledShaderPath	= file::path(base).append("..\\Assets\\Shaders\\").lexically_normal();
        IconPath				= file::path(base).append("..\\Icons\\").lexically_normal();
		TerrainSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Terrain\\").lexically_normal();
		NodeEditorPath			= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\NodeEditor\\").lexically_normal();
		volumeProfilePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\VolumeProfile\\").lexically_normal();
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
			volumeProfilePath
		};

		for (const auto& path : paths)
		{
			if (!file::exists(path))
			{
				file::create_directories(path);
			}
		}
    }
};

class PathFinder
{
public:
	static inline void Initialize() noexcept
    {
        InternalPath::Initialize();
    }

	static inline file::path Relative()
	{
		return InternalPath::DataPath;
	}

	static inline file::path DumpPath()
	{
		return InternalPath::DumpPath;
	}

	static inline file::path Relative(const std::string_view& path)
    {
        return file::path(InternalPath::DataPath) / path;
    }

	static inline file::path RelativeToShader()
	{
		return file::path(InternalPath::ShaderSourcePath);
	}

	static inline file::path RelativeToShader(const std::string_view& path)
	{
		return file::path(InternalPath::ShaderSourcePath) / path;
	}

	static inline file::path RelativeToPrecompiledShader()
	{
		return file::path(InternalPath::PrecompiledShaderPath);
	}

    static inline file::path RelativeToExecutable(const std::string_view& path)
    {
        return file::path(InternalPath::ExecuteablePath) / path;
    }

	static inline file::path RelativeToMaterial(const std::string_view& path)
	{
		return file::path(InternalPath::MaterialSourcePath) / path;
	}

    static inline file::path ShaderPath()
    {
        return InternalPath::ShaderSourcePath;
    }

	static inline file::path IconPath()
	{
		return InternalPath::IconPath;
	}

	static inline std::wstring MsbuildPreviewPath()
	{
		return InternalPath::MsbuildPreviewExe;
	}

    static inline std::wstring MsbuildPath()
    {
        return InternalPath::MsbuildExe;
    }

	static inline file::path ModelSourcePath()
	{
		return InternalPath::ModelSourcePath;
	}

	static inline file::path TextureSourcePath()
	{
		return InternalPath::TextureSourcePath;
	}

	static inline file::path UISourcePath()
	{
		return InternalPath::UISourcePath;
	}

	static inline file::path PrefabSourcePath()
	{
		return InternalPath::PrefabSourcePath;
	}

	static inline file::path MaterialSourcePath()
	{
		return InternalPath::MaterialSourcePath;
	}

	static inline file::path BaseProjectPath()
	{
		return InternalPath::BaseProjectPath;
	}

	static inline file::path VolumeProfilePath()
	{
		return InternalPath::volumeProfilePath;
	}

	static inline file::path DynamicSolutionPath(const std::string_view& path)
	{
		return file::path(InternalPath::DynamicSolutionDir) / path;
	}

	static inline file::path ProjectSettingPath(const std::string_view& path)
	{
		return file::path(InternalPath::ProjectSettingsPath) / path;
	}

	static inline file::path TerrainSourcePath(const std::string_view& path)
	{
		return file::path(InternalPath::TerrainSourcePath) / path;
	}

	static inline file::path NodeEditorPath(const std::string_view& path)
	{
		return file::path(InternalPath::NodeEditorPath) / path;
	}

	static inline file::path RelativeToModel(const std::string_view& path)
	{
		return file::path(InternalPath::ModelSourcePath) / path;
	}

	static inline file::path RelativeToTexture(const std::string_view& path)
	{
		return file::path(InternalPath::TextureSourcePath) / path;
	}

	static inline file::path RelativeToUISource(const std::string_view& path)
	{
		return file::path(InternalPath::UISourcePath) / path;
	}

	static inline file::path RelativeToPrefab(const std::string_view& path)
	{
		return file::path(InternalPath::PrefabSourcePath) / path;
	}

	static inline file::path RelativeToBaseProject(const std::string_view& path)
	{
		return file::path(InternalPath::BaseProjectPath) / path;
	}

	static inline file::path RelativeToVolumeProfile(const std::string_view& path)
	{
		return file::path(InternalPath::volumeProfilePath) / path;
	}
};
