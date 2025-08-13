// MeshSpawnModuleCS.h
#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

struct alignas(16) MeshParticleTemplateParams
{
	float lifeTime;
	float3 Scale;

	float3 RotationSpeed;
	float pad2;

	float3 InitialRotation;
	float pad4;

	float4 color;

	float3 velocity;
	float VerticalVelocity;

	float3 acceleration;
	float pad5;

	float horizontalVelocityRange;
	UINT textureIndex;
	UINT renderMode;
	float pad6;
};

class MeshSpawnModuleCS : public ParticleModule , public ISerializable
{
private:
	ID3D11ComputeShader* m_computeShader;  // "MeshSpawnModule" 로드

	// 상수 버퍼들 (동일)
	ID3D11Buffer* m_spawnParamsBuffer;
	ID3D11Buffer* m_templateBuffer;

	// 난수 및 시간 관리 버퍼들 (동일)
	ID3D11Buffer* m_randomStateBuffer;
	ID3D11UnorderedAccessView* m_randomStateUAV;

	SpawnParams m_spawnParams;                           // 동일
	MeshParticleTemplateParams m_meshParticleTemplate;   // 변경!

	// 상태 관리 (동일)
	bool m_spawnParamsDirty;
	bool m_templateDirty;
	UINT m_particleCapacity;

	// 난수 생성기 (동일)
	std::random_device m_randomDevice;
	std::mt19937 m_randomGenerator;
	std::uniform_real_distribution<float> m_uniform;

	Mathf::Vector3 m_previousEmitterPosition;
	bool m_forcePositionUpdate;

	std::mutex m_resetMutex;

	XMFLOAT3 m_originalEmitterSize;
	XMFLOAT3 m_originalParticleScale;
public:
	MeshSpawnModuleCS();
	virtual ~MeshSpawnModuleCS();

	// ParticleModule 인터페이스 구현
	virtual void Initialize() override;
	virtual void Update(float deltaTime) override;
	virtual void Release() override;
	virtual void OnSystemResized(UINT maxParticles) override;
	virtual void OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition) override;

	virtual void ResetForReuse();
	virtual bool IsReadyForReuse() const;

	void SetEmitterPosition(const Mathf::Vector3& position);
	void SetEmitterRotation(const Mathf::Vector3& rotation);
	void SetEmitterScale(const Mathf::Vector3& scale);

	// 스폰 설정 메서드들
	void SetSpawnRate(float rate);
	void SetEmitterType(EmitterType type);
	void SetEmitterSize(const XMFLOAT3& size);
	void SetEmitterRadius(float radius);

	// 3D 메시 관련 설정 메서드들
	void SetParticleLifeTime(float lifeTime);
	void SetParticleScale(const XMFLOAT3& Scale);
	void SetParticleRotationSpeed(const XMFLOAT3& Speed);
	void SetParticleInitialRotation(const XMFLOAT3& Rot);
	void SetParticleColor(const XMFLOAT4& color);
	void SetParticleVelocity(const XMFLOAT3& velocity);
	void SetParticleAcceleration(const XMFLOAT3& acceleration);
	void SetVelocity(float Vertical, float horizontalRange);
	void SetTextureIndex(UINT textureIndex);
	void SetRenderMode(UINT mode);
	
	// 상태 조회
	const SpawnParams& GetSpawnParams() const { return m_spawnParams; }
	float GetSpawnRate() const { return m_spawnParams.spawnRate; }
	EmitterType GetEmitterType() const { return static_cast<EmitterType>(m_spawnParams.emitterType); }
	MeshParticleTemplateParams GetTemplate() const { return m_meshParticleTemplate; }

	// 직렬화
public:

	virtual nlohmann::json SerializeData() const override;
	virtual void DeserializeData(const nlohmann::json& json) override;
	virtual std::string GetModuleType() const override;

private:
	bool InitializeComputeShader();   // "MeshSpawnModule" 셰이더 로드
	bool CreateConstantBuffers();     // MeshParticleTemplateParams 크기로 변경
	bool CreateUtilityBuffers();      // 동일
	void UpdateConstantBuffers(float deltaTime);  // 동일
	void ReleaseResources();          // 동일
};
