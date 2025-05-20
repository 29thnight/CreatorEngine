#pragma once
#include "../IRenderPass.h"
#include "ParticleSystem.h"

enum class Effect
{
	Test,
	Sparkle,
};

class EffectManager : public IRenderPass
{
public:
	EffectManager() {}

	virtual ~EffectManager() {}

	void Initialize();

	virtual void Execute(RenderScene& scene, Camera& camera);

	virtual void Render(RenderScene& scene, Camera& camera) {};

	void Update(float delta);

	void MakeEffects(Effect type, std::string_view name, Mathf::Vector3 pos, int maxParticle = 100);

	ParticleSystem* GetEffect(std::string_view name);

	bool RemoveEffect(std::string_view name);

	void InitializeImgui();
protected:
	ComPtr<ID3D11Buffer> m_constantBuffer{};
private:

	ID3D11Buffer* billboardVertexBuffer = nullptr;

	ComPtr<ID3D11Buffer> m_InstanceBuffer;
	ComPtr<ID3D11Buffer> m_ModelBuffer;			// world view projÀü¿ë
	
	std::unordered_map<std::string, std::unique_ptr<ParticleSystem>> effects;

};




