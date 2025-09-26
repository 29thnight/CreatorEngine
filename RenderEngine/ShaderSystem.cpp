#ifndef DYNAMICCPP_EXPORTS
#include "ShaderSystem.h"
#include "HLSLCompiler.h"
#include "Benchmark.hpp"
#include "ProgressWindow.h"
#include "ShaderPSO.h"
#include "ShaderDSL.h"
#include "VisualShaderDSL.h"
#include "VisualShaderPSO.h"
#include "DataSystem.h"
#include "ImGuiRegister.h"
#include "Material.h"
#include "ImageComponent.h"

#include <fstream>
ShaderResourceSystem::~ShaderResourceSystem()
{
}

void ShaderResourceSystem::Initialize()
{
	HLSLIncludeReloadShaders();
	CSOCleanup();
	LoadShaders();
	RegisterSelectShaderContext();
	m_isReloading = false;
}

void ShaderResourceSystem::Finalize()
{
	RemoveShaders();
	HLSLCompiler::CleanUpCache();
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
		std::cout << "Could not load shaders" << e.what() << std::endl;
	}
	catch (const std::exception& e)
	{
		Debug->LogWarning("Error" + std::string(e.what()));
		std::cout << "Error" << e.what() << std::endl;
	}

	LoadShaderAssets();
}

void ShaderResourceSystem::ReloadShaders()
{
	//RemoveShaders();
	g_progressWindow->Launch();
	g_progressWindow->SetTitle(L"Reloading shaders...");
	g_progressWindow->SetStatusText(L"Reloading shaders...");
	g_progressWindow->SetProgress(0.0f);
	HLSLCompiler::CleanUpCache();
	HLSLIncludeReloadShaders();
	CSOCleanup();

	try
	{
		file::path shaderpath = PathFinder::RelativeToShader();
		file::path precompiledpath = PathFinder::RelativeToPrecompiledShader();
		std::vector<file::path> hlslFiles;
		for (auto& dir : file::recursive_directory_iterator(shaderpath))
		{
			if (!dir.is_directory() && dir.path().extension() == ".hlsl")
			{
				hlslFiles.push_back(dir.path());
			}
		}

		size_t total = hlslFiles.size();
		size_t current = 0;

		for (const auto& hlslPath : hlslFiles)
		{
			file::path cso = precompiledpath / (hlslPath.stem().string() + ".cso");
			std::wstring text = L"Reloading shader : " + hlslPath.stem().wstring() + L"...";
			g_progressWindow->SetStatusText(text);

			if (file::exists(cso))
			{
				auto hlslTime = file::last_write_time(hlslPath);
				auto csoTime = file::last_write_time(cso);

				if (hlslTime > csoTime)
				{
					ReloadShaderFromPath(hlslPath);
				}
				else
				{
					ReloadShaderFromPath(cso);
				}
			}
			else
			{
				ReloadShaderFromPath(hlslPath);
			}

			++current;
			float percent = (static_cast<float>(current) / total) * 100.0f;
			g_progressWindow->SetProgress(percent);
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

	ReloadShaderAssets();

	m_shaderReloadedDelegate.Broadcast();

	m_isReloading = false;

	Debug->Log("[Shaders Reload Completed]");
	g_progressWindow->SetStatusText(L"Reloading shaders completed");
	g_progressWindow->SetProgress(100.0f);
	g_progressWindow->Close();
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

static bool ExtractStageAndName(const file::path& hlsl, std::string& stage, std::string& name)
{
static bool ReadTextFile(const file::path& path, std::string& out)
{
        std::ifstream fileStream(path);
        if (!fileStream)
                return false;

        out.assign((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
        return true;
}

        ShaderAssets.clear();
        VisualShaderAssets.clear();

        auto invalidateMaterials = [&](const std::string& assetName)
        {
                auto& materials = DataSystems->Materials;
                for (auto& [matName, mat] : materials)
                {
                        if (mat->GetShaderPSO() && mat->GetShaderPSO()->m_shaderPSOName == assetName)
                        {
                                mat->GetShaderPSO()->SetInvalidated(true);
                        }
                }

                ShaderAssets.erase(assetName);
                VisualShaderAssets.erase(assetName);
        };

        try
        {
                file::path shaderpath = PathFinder::RelativeToShader();
                for (auto& dir : file::recursive_directory_iterator(shaderpath))
                {
                        if (dir.is_directory())
                                continue;

                        const auto ext = dir.path().extension();
                        if (ext == ".shader")
                        {
                                std::string src;
                                if (!ReadTextFile(dir.path(), src))
                                        continue;

                                ShaderAssetDesc desc{};
                                if (!ParseShaderDSL(src, desc))
                                        continue;

                                std::string assetName = !desc.name.empty() ? desc.name : dir.path().stem().string();

                                auto pso = BuildPSOFromDesc(desc);
                                if (pso)
                                {
                                        pso->m_shaderPSOName = assetName;
                                        pso->SetInvalidated(false);
                                        ShaderAssets[assetName] = pso;
                                }
                                else
                                {
                                        invalidateMaterials(assetName);
                                }
                        }
                        else if (ext == ".vshader")
                        {
                                std::string src;
                                if (!ReadTextFile(dir.path(), src))
                                        continue;

                                VisualShaderGraphDesc graph{};
                                if (!ParseVisualShaderDSL(src, graph))
                                        continue;

                                std::string assetName = !graph.name.empty() ? graph.name : dir.path().stem().string();
                                graph.name = assetName;

                                if (graph.shaderAssetPath.empty())
                                {
                                        Debug->LogWarning("Visual shader '" + assetName + "' does not specify a ShaderAsset.");
                                        continue;
                                }

                                file::path basePath = PathFinder::RelativeToShader() / graph.shaderAssetPath;
                                std::string baseSource;
                                if (!ReadTextFile(basePath, baseSource))
                                {
                                        Debug->LogWarning("Visual shader '" + assetName + "' cannot open base shader asset: " + basePath.string());
                                        continue;
                                }

                                ShaderAssetDesc baseDesc{};
                                if (!ParseShaderDSL(baseSource, baseDesc))
                                {
                                        Debug->LogWarning("Visual shader '" + assetName + "' failed to parse base shader asset: " + basePath.string());
                                        continue;
                                }

                                if (!graph.queueTag.empty())
                                        baseDesc.pass.queueTag = graph.queueTag;
                                else
                                        graph.queueTag = baseDesc.pass.queueTag;

                                if (!graph.keywords.empty())
                                        baseDesc.pass.keywords = graph.keywords;
                                else
                                        graph.keywords = baseDesc.pass.keywords;

                                if (!graph.tag.empty())
                                        baseDesc.tag = graph.tag;
                                else
                                        graph.tag = baseDesc.tag;

                                baseDesc.name = assetName;

                                auto pso = BuildPSOFromDesc(baseDesc);
                                if (pso)
                                {
                                        pso->m_shaderPSOName = assetName;
                                        pso->SetInvalidated(false);
                                        ShaderAssets[assetName] = pso;
                                        VisualShaderAssets[assetName] = std::make_shared<VisualShaderPSO>(pso, graph, baseDesc);
                                }
                                else
                                {
                                        invalidateMaterials(assetName);
                                }
                        }
                }
        }
        catch (const file::filesystem_error& e)
        {
                Debug->LogWarning("Could not load shader assets" + std::string(e.what()));
        }
        catch (const std::exception& e)
        {
                Debug->LogWarning("Error" + std::string(e.what()));
        }

        for (auto& [name, pso] : ShaderAssets)
        {
                file::path shaderpath = PathFinder::RelativeToShader() / (name + ".shader");
                if (!file::exists(shaderpath))
                {
                        shaderpath = PathFinder::RelativeToShader() / (name + ".vshader");
                }
                if (file::exists(shaderpath))
                {
                        pso->SetShaderPSOGuid(DataSystems->GetFileGuid(shaderpath));
                }
        }
		file::path shaderpath = PathFinder::RelativeToShader();
		for (auto& dir : file::recursive_directory_iterator(shaderpath))
		{
			if (dir.is_directory() || dir.path().extension() != ".shader") continue;

			// 스크립트 읽기
			std::string src;
			{
				std::ifstream f(dir.path());
				if (!f) continue;
				src.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			}
			ShaderAssetDesc desc{};
			if (!ParseShaderDSL(src, desc)) continue;

			// 이름 없으면 파일명에서 추출
			std::string assetName = !desc.name.empty() ? desc.name : dir.path().stem().string();

			// PSO 만들고 등록
			auto pso = BuildPSOFromDesc(desc);
			if (pso)
			{
				pso->m_shaderPSOName = assetName;
				pso->SetInvalidated(false);
				ShaderAssets[assetName] = pso;
			}
			else
			{
				auto& materials = DataSystems->Materials;
				for (auto& [matName, mat] : materials)
				{
					if (mat->GetShaderPSO() && mat->GetShaderPSO()->m_shaderPSOName == assetName)
					{
						//mat->SetShaderPSO(nullptr);
						mat->GetShaderPSO()->SetInvalidated(true);
					}
				}

				ShaderAssets.erase(assetName);
			}
		}
	}
	catch (const file::filesystem_error& e)
	{
		Debug->LogWarning("Could not load shader assets" + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		Debug->LogWarning("Error" + std::string(e.what()));
	}
}

void ShaderResourceSystem::ReloadShaderAssets()
{
	LoadShaderAssets();
}

void ShaderResourceSystem::SetPSOs_GUID()
{
	for (auto& [name, pso] : ShaderAssets)
	{
		file::path shaderpath = PathFinder::RelativeToShader() / (name + ".shader");
		if (file::exists(shaderpath))
		{
			pso->SetShaderPSOGuid(DataSystems->GetFileGuid(shaderpath));
		}
	}
}

void ShaderResourceSystem::RegisterSelectShaderContext()
{
	ImGui::ContextRegister("SelectShader", true, [this]() {
		ImGui::Text("Select Shader");
		if (ImGui::BeginListBox("##ShaderList"))
		{
			if (ImGui::Selectable("None"))
			{
				if (m_selectShaderTarget)
				{
					m_selectShaderTarget->SetShaderPSO(nullptr);
					m_selectShaderTarget = nullptr;
				}
				ImGui::GetContext("SelectShader").Close();
			}
			for (auto& [name, pso] : ShaderAssets)
			{
				if (ImGui::Selectable(name.c_str()))
				{
					if (m_selectShaderTarget)
					{
						m_selectShaderTarget->SetShaderPSO(pso);
						m_selectShaderTarget = nullptr;
					}
					ImGui::GetContext("SelectShader").Close();
				}
			}
			ImGui::EndListBox();
		}
	});
	ImGui::GetContext("SelectShader").Close();

	ImGui::ContextRegister("SelectImageCustomShader", true, [this]() {
		ImGui::Text("Select PixelShader");
		if (ImGui::BeginListBox("##PixelShaderList"))
		{
			for (auto& [name, shader] : PixelShaders)
			{
				if (ImGui::Selectable(name.c_str()))
				{
					if (m_selectImageTarget)
					{
						m_selectImageTarget->SetCustomPixelShader(name);
						m_selectImageTarget = nullptr;
					}
					ImGui::GetContext("SelectImageCustomShader").Close();
				}
			}
			ImGui::EndListBox();
		}
	});
	ImGui::GetContext("SelectImageCustomShader").Close();
}

void ShaderResourceSystem::SetShaderSelectionTarget(Material* material)
{
	m_selectShaderTarget = material;
}

void ShaderResourceSystem::ClearShaderSelectionTarget()
{
	m_selectShaderTarget = nullptr;
}

void ShaderResourceSystem::SetImageSelectionTarget(ImageComponent* image)
{
	m_selectImageTarget = image;
}

void ShaderResourceSystem::ClearImageSelectionTarget()
{
	m_selectImageTarget = nullptr;
}

void ShaderResourceSystem::AddShaderFromPath(const file::path& filepath)
{
	ComPtr<ID3DBlob> blob{};
	try
	{
		blob = HLSLCompiler::LoadFormFile(filepath.string());
	}
	catch (const std::exception& e)
	{
		Debug->LogError("Failed to load shader: " + filepath.string() + "\n[shader compile logs] : \n" + e.what());
		return;
	}
	file::path filename = filepath.filename();
	std::string ext = filename.replace_extension().extension().string();
	filename.replace_extension();
	ext.erase(0, 1);

	AddShader(filename.string(), ext, blob);
}

void ShaderResourceSystem::ReloadShaderFromPath(const file::path& filepath)
{
	ComPtr<ID3DBlob> blob{};
	try
	{
		blob = HLSLCompiler::LoadFormFile(filepath.string());
		file::path filename = filepath.filename();
		std::string ext = filename.replace_extension().extension().string();
		filename.replace_extension();
		ext.erase(0, 1);

		ReloadShader(filename.string(), ext, blob);
	}
	catch (const std::exception& e)
	{
		Debug->LogError("Failed to load shader: " + filepath.string() + "\n[shader compile logs] : \n" + e.what());
		file::path filename = filepath.filename();
		std::string ext = filename.replace_extension().extension().string();
		filename.replace_extension();
		ext.erase(0, 1);

		EraseShader(filename.string(), ext);
		return;
	}
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

void ShaderResourceSystem::EraseShader(const std::string& name, const std::string& ext)
{
	if(ext == "vs")
	{
		VertexShaders.erase(name);
	}
	else if (ext == "hs")
	{
		HullShaders.erase(name);
	}
	else if (ext == "ds")
	{
		DomainShaders.erase(name);
	}
	else if (ext == "gs")
	{
		GeometryShaders.erase(name);
	}
	else if (ext == "ps")
	{
		PixelShaders.erase(name);
	}
	else if (ext == "cs")
	{
		ComputeShaders.erase(name);
	}
	else
	{
		throw std::runtime_error("Unknown shader type");
	}
}

void ShaderResourceSystem::ReloadShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob)
{
	if (ext == "vs")
	{
		VertexShader& vs = VertexShaders[name];
		vs.SwapAndReCompile(blob);
	}
	else if (ext == "hs")
	{
		HullShader& hs = HullShaders[name];
		hs.SwapAndReCompile(blob);
	}
	else if (ext == "ds")
	{
		DomainShader& ds = DomainShaders[name];
		ds.SwapAndReCompile(blob);
	}
	else if (ext == "gs")
	{
		GeometryShader& gs = GeometryShaders[name];
		gs.SwapAndReCompile(blob);
	}
	else if (ext == "ps")
	{
		PixelShader& ps = PixelShaders[name];
		ps.SwapAndReCompile(blob);
	}
	else if (ext == "cs")
	{
		ComputeShader& cs = ComputeShaders[name];
		cs.SwapAndReCompile(blob);
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
	VisualShaderAssets.clear();
}
#endif // !DYNAMICCPP_EXPORTS