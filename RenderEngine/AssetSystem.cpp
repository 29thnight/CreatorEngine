#include "AssetSystem.h"
#include "HLSLCompiler.h"

AssetSystem::~AssetSystem()
{
}

void AssetSystem::Initialize()
{
}

void AssetSystem::LoadShaders()
{
	try
	{
		file::path shaderpath = PathFinder::Relative("Shaders\\");
		for (auto& dir : file::recursive_directory_iterator(shaderpath))
		{
			if (dir.is_directory())
				continue;

			if (dir.path().extension() == ".hlsl")
			{
				AddShaderFromPath(dir.path());
			}
		}
	}
	catch (const file::filesystem_error& e)
	{
		Log::Warning("Could not load shaders" + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		Log::Warning("Error" + std::string(e.what()));
	}
}

void AssetSystem::AddShaderFromPath(const file::path& filepath)
{
	ComPtr<ID3DBlob> blob = HLSLCompiler::LoadFormFile(filepath.string());
	file::path filename = filepath.filename();
	std::string ext = filename.replace_extension().extension().string();
	filename.replace_extension();
	ext.erase(0, 1);

	AddShader(filename.string(), ext, blob);
}

void AssetSystem::AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob)
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

void AssetSystem::RemoveShaders()
{
	VertexShaders.clear();
	HullShaders.clear();
	DomainShaders.clear();
	GeometryShaders.clear();
	PixelShaders.clear();
	ComputeShaders.clear();
}
