#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Shader.h"
#include "Delegate.h"
#include "DLLAcrossSingleton.h"
#include "VisualShaderPSO.h"
#include "Core.Thread.hpp"
#include <memory>

//class VisualShaderPSO; // visual shader pipeline
//        
class ShaderPSO; // Àü¹æ ¼±¾ð
class Material;
class ImageComponent;
class ShaderResourceSystem final : public DLLCore::Singleton<ShaderResourceSystem>
{
private:
	friend class DLLCore::Singleton<ShaderResourceSystem>;

	ShaderResourceSystem() = default;
	~ShaderResourceSystem();

public:
	void Initialize();
	void Finalize();
	void LoadShaders();
	void ReloadShaders();
	void HLSLIncludeReloadShaders();
	void CSOCleanup();
	void CSOAllCleanup();

	bool IsReloading() const { return m_isReloading; }
	void SetReloading(bool reloading) { m_isReloading = reloading; }

	void LoadShaderAssets();
	void ReloadShaderAssets();
	void SetPSOs_GUID();

	void RegisterSelectShaderContext();
	void SetShaderSelectionTarget(Material* material);
	void ClearShaderSelectionTarget();

	void SetImageSelectionTarget(ImageComponent* image);
	void ClearImageSelectionTarget();

	std::unordered_map<std::string, VertexShader>	VertexShaders;
	std::unordered_map<std::string, HullShader>		HullShaders;
	std::unordered_map<std::string, DomainShader>	DomainShaders;
	std::unordered_map<std::string, GeometryShader>	GeometryShaders;
	std::unordered_map<std::string, PixelShader>	PixelShaders;
	std::unordered_map<std::string, ComputeShader>	ComputeShaders;

	std::unordered_map<std::string, std::shared_ptr<ShaderPSO>> ShaderAssets;
	std::unordered_map<std::string, std::shared_ptr<VisualShaderPSO>> VisualShaderAssets;

	Core::Delegate<void> m_shaderReloadedDelegate;

	// Shader loading
	void AddShaderFromPath(const file::path& filepath);
	void ReloadShaderFromPath(const file::path& filepath);
private:
	void AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void EraseShader(const std::string& name, const std::string& ext);
	void ReloadShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void RemoveShaders();

	bool m_isReloading = false;
	Material* m_selectShaderTarget = nullptr;
	ImageComponent* m_selectImageTarget = nullptr;
	ThreadPool<std::function<void()>>* m_shaderReloadThreadPool = nullptr;
	std::mutex m_shaderReloadMutex;
};

static auto ShaderSystem = ShaderResourceSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS