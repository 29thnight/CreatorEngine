#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"
#include "ISerializable.h"

struct alignas(16) MeshMovementParams
{
	float deltaTime;
	float gravityStrength;
	int useGravity;
	int velocityMode;

	float currentTime;
	Mathf::Vector3 windDirection;

	float windStrength;
	float turbulence;
	float frequency;
	float pad1;

	Mathf::Vector3 orbitalCenter;
	float orbitalRadius;

	float orbitalSpeed;
	Mathf::Vector3 orbitalAxis;

	float explosiveSpeed;
	float explosiveDecay;
	float explosiveRandom;
	float explosiveSphere;

	int velocityCurveSize;
	int impulseCount;
	float2 pad2;

	float3 emitterPosition;
	float emitterPad1;
	float3 emitterRotation;
	float emitterPad2;
};

class MeshMovementModuleCS : public ParticleModule, public ISerializable
{
private:
	// 컴퓨트 셰이더 리소스
	ID3D11ComputeShader* m_computeShader;

	// 상수 버퍼
	ID3D11Buffer* m_movementParamsBuffer;

	VelocityMode m_velocityMode;
	std::vector<VelocityPoint> m_velocityCurve;
	std::vector<ImpulseData> m_impulses;
	WindData m_windData;
	OrbitalData m_orbitalData;
	ExplosiveData m_explosiveData;

	float m_currentTime;

	// 시스템 상태
	UINT m_particleCapacity;

	// 이징 모듈
	EaseInOut m_easingModule;

	// Structured buffers
	ID3D11Buffer* m_velocityCurveBuffer;
	ID3D11ShaderResourceView* m_velocityCurveSRV;
	ID3D11Buffer* m_impulsesBuffer;
	ID3D11ShaderResourceView* m_impulsesSRV;

	// 상태 추적
	bool m_paramsDirty;

public:
	MeshMovementParams m_movementParams;
	bool m_easingEnable;
public:
	MeshMovementModuleCS();
	virtual ~MeshMovementModuleCS();

	void Initialize() override;
	void Update(float delta) override;
	void OnSystemResized(UINT max) override;
	void Release() override;

	virtual void ResetForReuse();
	virtual bool IsReadyForReuse() const;

	void SetEmitterTransform(const Mathf::Vector3& position, const Mathf::Vector3& rotation);

	// Movement settings
	void SetUseGravity(bool use);
	bool GetUseGravity() const;
	void SetGravityStrength(float strength);
	void SetEasingEnabled(bool enabled);
	void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);
	void DisableEasing();

	// Velocity 관련
	void SetVelocityMode(VelocityMode mode);
	void SetVelocityCurve(const std::vector<VelocityPoint>& curve);
	void AddVelocityPoint(float time, const Mathf::Vector3& velocity, float strength = 1.0f);
	void AddImpulse(float triggerTime, const Mathf::Vector3& direction, float force, float duration, float impulseRange = 0.5f, UINT impulseType = 1);
	void SetWindEffect(const Mathf::Vector3& direction, float strength, float turbulence = 0.5f, float frequency = 1.0f);
	void SetOrbitalMotion(const Mathf::Vector3& center, float radius, float speed, const Mathf::Vector3& axis = Mathf::Vector3(0, 1, 0));
	void SetExplosiveEffect(float initialSpeed = 50.0f, float speedDecay = 2.0f, float randomFactor = 0.4f, float sphereRadius = 1.0f);

	VelocityMode GetVelocityMode() const { return m_velocityMode; }
	const std::vector<VelocityPoint>& GetVelocityCurve() const { return m_velocityCurve; }
	const std::vector<ImpulseData>& GetImpulses() const { return m_impulses; }
	const WindData& GetWindData() const { return m_windData; }
	const OrbitalData& GetOrbitalData() const { return m_orbitalData; }
	const ExplosiveData& GetExplosiveData() const { return m_explosiveData; }

	// ISerializable
	virtual nlohmann::json SerializeData() const override;
	virtual void DeserializeData(const nlohmann::json& json) override;
	virtual std::string GetModuleType() const override;

	void ClearVelocityCurve();
	void ClearImpulses();

private:

	// 헬퍼 메서드들
	void UpdateConstantBuffers(float delta);
	void UpdateStructuredBuffers();
	bool InitializeCompute();

};

