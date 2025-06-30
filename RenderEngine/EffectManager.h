#pragma once
#include "../IRenderPass.h"
#include "ParticleSystem.h"
#include "EffectBase.h"

class EffectManager : public IRenderPass
{
private:
	static EffectManager* s_currentInstance;

public:
	EffectManager() {
		s_currentInstance = this;
	}

	~EffectManager() {
		if (s_currentInstance == this) {
			s_currentInstance = nullptr;
		}
	}

	static EffectManager* GetCurrent() {
		return s_currentInstance;
	}

	void Initialize();

	virtual void Execute(RenderScene& scene, Camera& camera);

	virtual void Render(RenderScene& scene, Camera& camera) {};

	void Update(float delta);

	EffectBase* GetEffect(std::string_view name);

	bool RemoveEffect(std::string_view name);

	void RegisterCustomEffect(const std::string& name, const std::vector<std::shared_ptr<ParticleSystem>>& emitters);

protected:
	ComPtr<ID3D11Buffer> m_constantBuffer{};
private:

	ID3D11Buffer* billboardVertexBuffer = nullptr;

	ComPtr<ID3D11Buffer> m_InstanceBuffer;
	ComPtr<ID3D11Buffer> m_ModelBuffer;			// world view projÀü¿ë
	
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> effects;

};




