#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"
#include "ISerializable.h"

struct alignas(16) MeshSizeParams
{
	Mathf::Vector3 startSize;
	float pad1;
	Mathf::Vector3 endSize;
	float deltaTime;

	int useRandomScale;
	float randomScaleMin;
	float randomScaleMax;
	UINT maxParticles;
	
	Mathf::Vector3 emitterScale;
	float pad2;
};

class MeshSizeModuleCS : public ParticleModule, public ISerializable
{
private:
	ID3D11ComputeShader* m_computeShader;
	ID3D11Buffer* m_sizeParamsBuffer;

	MeshSizeParams m_sizeParams;
	bool m_paramsDirty;

	EaseInOut m_easingModule;

	bool m_isInitialized;
	UINT m_particleCapacity;
public:

	bool m_easingEnable;
public:

	MeshSizeModuleCS();
	virtual ~MeshSizeModuleCS();

	// 기본 인터페이스
	void Initialize() override;
	void Update(float deltaTime) override;
	void Release() override;
	void OnSystemResized(UINT maxParticles) override;

	virtual void ResetForReuse();
	virtual bool IsReadyForReuse() const;

	virtual nlohmann::json SerializeData() const override;
	virtual void DeserializeData(const nlohmann::json& json) override;
	virtual std::string GetModuleType() const override;

	void SetEmitterTransform(Mathf::Vector3 scale) { m_sizeParams.emitterScale = scale; m_paramsDirty = true; }

	void SetStartSize(Mathf::Vector3 size) { m_sizeParams.startSize = size; m_paramsDirty = true; }
	Mathf::Vector3 GetStartSize() const { return m_sizeParams.startSize; }

	void SetEndSize(Mathf::Vector3 size) { m_sizeParams.endSize = size; m_paramsDirty = true; }
	Mathf::Vector3 GetEndSize() const { return m_sizeParams.endSize; }

	bool GetUseRandomScale() const { return m_sizeParams.useRandomScale; }
	float GetRandomScaleMin() const { return m_sizeParams.randomScaleMin; }
	float GetRandomScaleMax() const { return m_sizeParams.randomScaleMax; }

	void SetRandomScale(bool useRandomScale, float min, float max);

	void SetEasingEnabled(bool enabled);
	void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);
	void DisableEasing();

private:
	// 초기화 메서드
	bool InitializeComputeShader();
	bool CreateConstantBuffers();

	// 업데이트 메서드
	void UpdateConstantBuffers();

	// 리소스 정리
	void ReleaseResources();

};

