#pragma once
#include "IRenderPass.h"
#include "ParticleSystem.h"
#include "EffectBase.h"
#include "DLLAcrossSingleton.h"

class EffectManager : public IRenderPass, public DLLCore::Singleton<EffectManager>
{
private:
	friend class DLLCore::Singleton<EffectManager>;
	EffectManager() = default;
	~EffectManager() = default;

public:
	void Initialize();

	virtual void Execute(RenderScene& scene, Camera& camera);

	virtual void Render(RenderScene& scene, Camera& camera) {};

	void Update(float delta);

	EffectBase* GetEffect(std::string_view name);

	bool RemoveEffect(std::string_view name);

	void RegisterCustomEffect(const std::string& name, const std::vector<std::shared_ptr<ParticleSystem>>& emitters);

	void CreateEffectInstance(const std::string& templateName, const std::string& instanceName);

protected:
	ComPtr<ID3D11Buffer> m_constantBuffer{};
private:

	ID3D11Buffer* billboardVertexBuffer = nullptr;

	ComPtr<ID3D11Buffer> m_InstanceBuffer;
	ComPtr<ID3D11Buffer> m_ModelBuffer;			// world view projÀü¿ë
	
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> effects;
};

static inline auto EffectManagers = EffectManager::GetInstance();
