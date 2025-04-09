#pragma once
#include <filesystem>
#include <Windows.h>

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
	inline std::string VS2022Path{ "\"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\"" };
    inline file::path DynamicSolutionDir{};
	inline file::path BaseProjectPath{};

    inline void Initialize()
    {
        HMODULE hModule = GetModuleHandleW(NULL);
        WCHAR path[MAX_PATH]{};

        GetModuleFileNameW(hModule, path, MAX_PATH);
        file::path p(path);

        ExecuteablePath = p.remove_filename();


        auto base = file::path(ExecuteablePath);
		//TODO 지금은 이런식으로 불러오고 나중에는 기본 ini 설정값을 정해서 읽어오는 걸로 합시다.
		BaseProjectPath = file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
        DataPath = file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\").lexically_normal();

		ModelSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Models\\").lexically_normal();
		TextureSourcePath	= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Textures\\").lexically_normal();
		MaterialSourcePath	= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\Materials\\").lexically_normal();
		UISourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Assets\\UI\\").lexically_normal();

        ShaderSourcePath		= file::path(base).append("..\\..\\Dynamic_CPP\\Shaders\\").lexically_normal();
		DynamicSolutionDir		= file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
		PrecompiledShaderPath	= file::path(base).append("..\\Assets\\Shaders\\").lexically_normal();
        IconPath				= file::path(base).append("..\\Icons\\").lexically_normal();

		//dir not exist -> create dir
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

	static inline std::string VS2022Path()
	{
		return InternalPath::VS2022Path;
	}

	static inline file::path DynamicSolutionPath(const std::string_view& path)
	{
		return file::path(InternalPath::DynamicSolutionDir) / path;
	}
};
