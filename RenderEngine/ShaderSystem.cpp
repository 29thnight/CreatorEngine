#ifndef DYNAMICCPP_EXPORTS
#include "ShaderSystem.h"
#include "HLSLCompiler.h"
#include "Benchmark.hpp"
#include "ProgressWindow.h"
#include "ShaderPSO.h"
#include "ShaderDSL.h"
#include "DataSystem.h"
#include "ImGuiRegister.h"
#include "Material.h"
#include "ImageComponent.h"

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
	// 규칙: <Name>.<stage>.hlsl  (예: "VertexShader.vs.hlsl")
	if (hlsl.extension() != ".hlsl") return false;
	file::path stem1 = hlsl.stem();         // "VertexShader.vs"
	file::path stem2 = stem1.stem();        // "VertexShader"
	file::path stageExt = stem1.extension();// ".vs"
	if (stageExt.empty()) return false;
	stage = stageExt.string();
	if (!stage.empty() && stage[0] == '.') stage.erase(0, 1); // "vs"
	name = stem2.string();                                 // "VertexShader"
	return !stage.empty() && !name.empty();
}

// NEW: 개별 스크립트 처리
static std::shared_ptr<ShaderPSO> BuildPSOFromDesc(const ShaderAssetDesc& desc)
{
	auto pso = std::make_shared<ShaderPSO>();

	auto bindFile = [&](const std::string& path) {
		if (path.empty()) return;
		file::path p = PathFinder::RelativeToShader() / path; // Shader 폴더 기준
		// 필요시 먼저 컴파일/등록 (이미 등록돼있으면 내부에서 덮어씀)
		try { ShaderSystem->AddShaderFromPath(p); }
		catch (...) {}

		std::string stage, base;
		if (!ExtractStageAndName(p, stage, base)) return;

		if		(stage == "vs") pso->m_vertexShader		= &ShaderSystem->VertexShaders[base];
		else if (stage == "ps") pso->m_pixelShader		= &ShaderSystem->PixelShaders[base];
		else if (stage == "gs") pso->m_geometryShader	= &ShaderSystem->GeometryShaders[base];
		else if (stage == "hs") pso->m_hullShader		= &ShaderSystem->HullShaders[base];
		else if (stage == "ds") pso->m_domainShader		= &ShaderSystem->DomainShaders[base];
		else if (stage == "cs") pso->m_computeShader	= &ShaderSystem->ComputeShaders[base];
	};

	bindFile(desc.pass.vs);
	bindFile(desc.pass.ps);
	bindFile(desc.pass.gs);
	bindFile(desc.pass.hs);
	bindFile(desc.pass.ds);
	bindFile(desc.pass.cs);

	if(!pso->m_vertexShader || !pso->m_pixelShader)
	{
		// 최소한 버텍스/픽셀 셰이더는 모두 유효해야 함
		Debug->LogWarning("ShaderPSO '" + desc.name + "' has no valid vertex/pixel/compute shader.");
		return nullptr;
	}

	// 자동 리플렉션으로 cbuffer/SRV 슬롯 생성
	pso->ReflectConstantBuffers();
	pso->CreateInputLayoutFromShader();

	// TODO: queueTag/keywords 렌더 큐/키워드 시스템과 연동(옵션)
	return pso;
}

void ShaderResourceSystem::LoadShaderAssets()
{
	ShaderAssets.clear();
	try {
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
}
#endif // !DYNAMICCPP_EXPORTS