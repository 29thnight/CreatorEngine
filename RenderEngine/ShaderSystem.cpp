#include "ShaderSystem.h"
#include "HLSLCompiler.h"
#include "Banchmark.hpp"

ShaderResourceSystem::~ShaderResourceSystem()
{
}

void ShaderResourceSystem::Initialize()
{
	HLSLIncludeReloadShaders();
	CSOCleanup();
	LoadShaders();
	m_isReloading = false;
}

void ShaderResourceSystem::LoadShaders()
{
	try
	{
		file::path shaderpath = PathFinder::RelativeToShader();
		file::path precompiledpath = PathFinder::RelativeToPrecompiledShader();
		for (auto& dir : file::recursive_directory_iterator(shaderpath))
		{
			if (dir.is_directory() || dir.path().extension() != ".hlsl")
				continue;

			file::path cso = precompiledpath.string() + dir.path().stem().string() + ".cso";

			if (file::exists(cso))
			{
				auto hlslTime = file::last_write_time(dir.path());
				auto csoTime = file::last_write_time(cso);

				if (hlslTime > csoTime)
				{
					AddShaderFromPath(dir.path());
				}
				else
				{
					AddShaderFromPath(cso);
				}
			}
			else
			{
				AddShaderFromPath(dir.path());
			}
		}
	}
	catch (const file::filesystem_error& e)
	{
		Debug->LogWarning("Could not load shaders" + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		Debug->LogWarning("Error" + std::string(e.what()));
	}
}

void ShaderResourceSystem::ReloadShaders()
{
	RemoveShaders();
	HLSLCompiler::CleanUpCache();
	HLSLIncludeReloadShaders();
	CSOCleanup();
	LoadShaders();
	m_isReloading = false;
}

void ShaderResourceSystem::HLSLIncludeReloadShaders()
{
	file::path shaderpath = PathFinder::RelativeToShader();
	file::path precompiledpath = PathFinder::RelativeToPrecompiledShader();
	//find max last_write_time -> if hlsliTime > csoTime
	//CSOCleanup();
	for (auto& dir : file::recursive_directory_iterator(shaderpath))
	{
		if (dir.is_directory() || dir.path().extension() != ".hlsli")
			continue;
		file::path cso = precompiledpath.string() + dir.path().stem().string() + ".cso";
		if (file::exists(cso))
		{
			auto hlsliTime = file::last_write_time(dir.path());
			auto csoTime = file::last_write_time(cso);
			if (hlsliTime > csoTime)
			{
				CSOAllCleanup();
				break;
			}
		}
	}
}

void ShaderResourceSystem::CSOCleanup()
{
	file::path shaderpath = PathFinder::RelativeToShader();
	file::path precompiledpath = PathFinder::RelativeToPrecompiledShader();
	for (auto& dir : file::recursive_directory_iterator(shaderpath))
	{
		if (dir.is_directory() || dir.path().extension() != ".hlsl")
			continue;
		file::path cso = precompiledpath.string() + dir.path().stem().string() + ".cso";
		if (file::exists(cso))
		{
			auto hlslTime = file::last_write_time(dir.path());
			auto csoTime = file::last_write_time(cso);

			if (hlslTime > csoTime)
			{
				file::remove(cso);
			}
		}
	}
}

void ShaderResourceSystem::CSOAllCleanup()
{
	file::path shaderpath = PathFinder::RelativeToShader();
	file::path precompiledpath = PathFinder::RelativeToPrecompiledShader();
	for (auto& dir : file::recursive_directory_iterator(shaderpath))
	{
		if (dir.is_directory() || dir.path().extension() != ".hlsl")
			continue;
		file::path cso = precompiledpath.string() + dir.path().stem().string() + ".cso";
		if (file::exists(cso))
		{
			file::remove(cso);
		}
	}
}

void ShaderResourceSystem::AddShaderFromPath(const file::path& filepath)
{
	ComPtr<ID3DBlob> blob = HLSLCompiler::LoadFormFile(filepath.string());
	file::path filename = filepath.filename();
	std::string ext = filename.replace_extension().extension().string();
	filename.replace_extension();
	ext.erase(0, 1);

	AddShader(filename.string(), ext, blob);
}

void ShaderResourceSystem::AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob)
{
	if (ext == "vs")
	{
		VertexShader vs = VertexShader(name, blob);
		vs.Compile();
		VertexShaders[name] = vs;
	}
	else if (ext == "hs")
	{
		HullShader hs = HullShader(name, blob);
		hs.Compile();
		HullShaders[name] = hs;
	}
	else if (ext == "ds")
	{
		DomainShader ds = DomainShader(name, blob);
		ds.Compile();
		DomainShaders[name] = ds;
	}
	else if (ext == "gs")
	{
		GeometryShader gs = GeometryShader(name, blob);
		gs.Compile();
		GeometryShaders[name] = gs;
	}
	else if (ext == "ps")
	{
		PixelShader ps = PixelShader(name, blob);
		ps.Compile();
		PixelShaders[name] = ps;
	}
	else if (ext == "cs")
	{
		ComputeShader cs = ComputeShader(name, blob);
		cs.Compile();
		ComputeShaders[name] = cs;
	}
	else
	{
		throw std::runtime_error("Unknown shader type");
	}
}

void ShaderResourceSystem::RemoveShaders()
{
	VertexShaders.clear();
	HullShaders.clear();
	DomainShaders.clear();
	GeometryShaders.clear();
	PixelShaders.clear();
	ComputeShaders.clear();
}
