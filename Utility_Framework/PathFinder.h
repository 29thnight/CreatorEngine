#pragma once
#include <filesystem>
#include <Windows.h>

inline const char* VSWHERE_PATH = R"(C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe)";

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
    inline void Initialize()
    {
        HMODULE hModule = GetModuleHandleW(NULL);
        WCHAR path[MAX_PATH]{};

        GetModuleFileNameW(hModule, path, MAX_PATH);
        file::path p(path);

        ExecuteablePath = p.remove_filename();

		DumpPath = file::path(ExecuteablePath).append("Dump\\").lexically_normal();

        auto base = file::path(ExecuteablePath);
		//TODO 지금은 이런식으로 불러오고 나중에는 기본 ini 설정값을 정해서 읽어오는 걸로 합시다.
		BaseProjectPath = file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
        DataPath = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\").lexically_normal();

		ModelSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Models\\").lexically_normal();
		TextureSourcePath	= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Textures\\").lexically_normal();
		MaterialSourcePath	= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Materials\\").lexically_normal();
		UISourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\UI\\").lexically_normal();
        ShaderSourcePath	= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Shaders\\").lexically_normal();

		DynamicSolutionDir		= file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
		ProjectSettingsPath		= file::path(base).append("..\\..\\Dynamic_CPP\\ProjectSetting").lexically_normal();

		PrecompiledShaderPath	= file::path(base).append("..\\Assets\\Shaders\\").lexically_normal();
        IconPath				= file::path(base).append("..\\Icons\\").lexically_normal();

		TerrainSourcePath = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Terrain\\").lexically_normal();
		NodeEditorPath = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\NodeEditor\\").lexically_normal();
		//dir not exist -> create dir
		if (!file::exists(DumpPath))
		{
			file::create_directories(DumpPath);
		}
		if (!file::exists(DataPath))
		{
			file::create_directories(DataPath);
		}
		if (!file::exists(ShaderSourcePath))
		{
			file::create_directories(ShaderSourcePath);
		}
		if (!file::exists(ModelSourcePath))
		{
			file::create_directories(ModelSourcePath);
		}
		if (!file::exists(TextureSourcePath))
		{
			file::create_directories(TextureSourcePath);
		}
		if (!file::exists(MaterialSourcePath))
		{
			file::create_directories(MaterialSourcePath);
		}
		if (!file::exists(UISourcePath))
		{
			file::create_directories(UISourcePath);
		}
		if (!file::exists(IconPath))
		{
			file::create_directories(IconPath);
		}
		if (!file::exists(DynamicSolutionDir))
		{
			file::create_directories(DynamicSolutionDir);
		}
		if (!file::exists(PrecompiledShaderPath))
		{
			file::create_directories(PrecompiledShaderPath);
		}
		if (!file::exists(ProjectSettingsPath))
		{
			file::create_directories(ProjectSettingsPath);
		}
		if (!file::exists(TerrainSourcePath))
		{
			file::create_directories(TerrainSourcePath);
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
};
