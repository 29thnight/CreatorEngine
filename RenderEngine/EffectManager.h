#pragma once
#include "IRenderPass.h"

class EffectBase;
class ParticleSystem;
class EffectManager : public IRenderPass, public Singleton<EffectManager>
{
private:
	friend class Singleton<EffectManager>;

public:
	EffectManager() = default;
	~EffectManager() = default;

	void Initialize();

	virtual void Execute(RenderScene& scene, Camera& camera);

	virtual void Render(RenderScene& scene, Camera& camera) {};

	void Update(float delta);

	EffectBase* GetEffect(std::string_view name);

	bool RemoveEffect(std::string_view name);

	void RegisterCustomEffect(const std::string& name, const std::vector<std::shared_ptr<ParticleSystem>>& emitters);

	void CreateEffectInstance(const std::string& templateName, const std::string& instanceName);

	// 읽기만 effects에 접근은 오로지 매니저에서만
	const std::unordered_map<std::string, std::unique_ptr<EffectBase>>& GetEffects() const { return effects; }
protected:
	ComPtr<ID3D11Buffer> m_constantBuffer{};
private:

	ID3D11Buffer* billboardVertexBuffer = nullptr;

	ComPtr<ID3D11Buffer> m_InstanceBuffer;
	ComPtr<ID3D11Buffer> m_ModelBuffer;			// world view proj전용
	
	static std::unordered_map<std::string, std::unique_ptr<EffectBase>> effects;
};

static inline auto& efm = EffectManager::GetInstance();
