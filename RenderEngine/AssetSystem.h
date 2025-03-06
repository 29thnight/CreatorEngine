#pragma once
#include "Shader.h"

class AssetSystem final : public Singleton<AssetSystem>
{
private:
	friend class Singleton;

	AssetSystem() = default;
	~AssetSystem();

public:
	void Initialize();
	void LoadShaders();

	std::unordered_map<std::string, VertexShader>	VertexShaders;
	std::unordered_map<std::string, HullShader>		HullShaders;
	std::unordered_map<std::string, DomainShader>	DomainShaders;
	std::unordered_map<std::string, GeometryShader>	GeometryShaders;
	std::unordered_map<std::string, PixelShader>	PixelShaders;
	std::unordered_map<std::string, ComputeShader>	ComputeShaders;

private:
	// Shader loading
	void AddShaderFromPath(const file::path& filepath);
	void AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void RemoveShaders();
};

static inline auto& AssetsSystems = AssetSystem::GetInstance();