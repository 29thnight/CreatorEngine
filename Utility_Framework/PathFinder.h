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
	inline file::path MaterialSourcePath{};
	inline std::string VS2022Path{ "\"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\"" };
    inline file::path DynamicSolutionDir{};

    inline void Initialize()
    {
        HMODULE hModule = GetModuleHandleW(NULL);
        WCHAR path[MAX_PATH]{};

        GetModuleFileNameW(hModule, path, MAX_PATH);
        file::path p(path);

        ExecuteablePath = p.remove_filename();

        auto base = file::path(ExecuteablePath);
        DataPath = file::path(base).append("..\\Assets\\").lexically_normal();
		MaterialSourcePath = file::path(base).append("..\\Assets\\Materials\\").lexically_normal();
        ShaderSourcePath = file::path(base).append("..\\Assets\\Shaders\\").lexically_normal();
		DynamicSolutionDir = file::path(base).append("..\\..\\Dynamic_CPP\\").lexically_normal();
		IconPath = file::path(base).append("..\\Icons\\").lexically_normal();
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
