#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Shader.h"
#include "Delegate.h"

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

	std::unordered_map<std::string, VertexShader>	VertexShaders;
	std::unordered_map<std::string, HullShader>		HullShaders;
	std::unordered_map<std::string, DomainShader>	DomainShaders;
	std::unordered_map<std::string, GeometryShader>	GeometryShaders;
	std::unordered_map<std::string, PixelShader>	PixelShaders;
	std::unordered_map<std::string, ComputeShader>	ComputeShaders;

	Core::Delegate<void> m_shaderReloadedDelegate;

private:
	// Shader loading
	void AddShaderFromPath(const file::path& filepath);
	void ReloadShaderFromPath(const file::path& filepath);
	void AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void ReloadShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void RemoveShaders();

	bool m_isReloading = false;
};

static inline auto& ShaderSystem = ShaderResourceSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS