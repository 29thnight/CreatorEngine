#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Shader.h"
#include "Delegate.h"

class ShaderPSO; // 전방 선언
class Material;
class ShaderResourceSystem final : public Singleton<ShaderResourceSystem>
{
private:
	friend class Singleton;

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

	void RegisterSelectShaderContext();
	void SetShaderSelectionTarget(Material* material);

	std::unordered_map<std::string, VertexShader>	VertexShaders;
	std::unordered_map<std::string, HullShader>		HullShaders;
	std::unordered_map<std::string, DomainShader>	DomainShaders;
	std::unordered_map<std::string, GeometryShader>	GeometryShaders;
	std::unordered_map<std::string, PixelShader>	PixelShaders;
	std::unordered_map<std::string, ComputeShader>	ComputeShaders;

	std::unordered_map<std::string, std::shared_ptr<ShaderPSO>> ShaderAssets;

	Core::Delegate<void> m_shaderReloadedDelegate;

	// Shader loading
	void AddShaderFromPath(const file::path& filepath);
	void ReloadShaderFromPath(const file::path& filepath);
private:
	void AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void ReloadShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void RemoveShaders();

	bool m_isReloading = false;
	Material* m_selectShaderTarget = nullptr;
};

static inline auto& ShaderSystem = ShaderResourceSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS