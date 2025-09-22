#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

class MovementModuleCS : public ParticleModule, public ISerializable
{
public:
	MovementModuleCS();

	~MovementModuleCS()
	{
		Release();
	}

	// ParticleModule methods
	void Initialize() override;
	void Update(float delta) override;
	void OnSystemResized(UINT max) override;
	void Release() override;

	virtual void ResetForReuse();
	virtual bool IsReadyForReuse() const;

	// Movement settings
	void SetUseGravity(bool use) { m_gravity = use; m_paramsDirty = true; }

	bool GetUseGravity() { return m_gravity; }

	void SetGravityStrength(float strength) { m_gravityStrength = strength; m_paramsDirty = true; }
	void SetEasingEnabled(bool enabled) { m_easingEnabled = enabled; m_paramsDirty = true; }
	void SetEasingType(int type) { m_easingType = type; m_paramsDirty = true; }

	void SetEmitterTransform(const Mathf::Vector3& position, const Mathf::Vector3& rotation);

	// Compute shader methods
	bool InitializeCompute();

	virtual nlohmann::json SerializeData() const override;
	virtual void DeserializeData(const nlohmann::json& json) override;
	virtual std::string GetModuleType() const override;

	// constant setting method
	void SetVelocityMode(VelocityMode mode);

	void SetVelocityCurve(const std::vector<VelocityPoint>& curve);

	void AddVelocityPoint(float time, const Mathf::Vector3& velocity, float strength = 1.0f);

	void AddImpulse(float triggerTime, const Mathf::Vector3& direction, float force, float duration = 0.1f);

	void SetWindEffect(const Mathf::Vector3& direction, float strength, float turbulence = 0.5f, float frequency = 1.0f);

	void SetOrbitalMotion(const Mathf::Vector3& center, float radius, float speed, const Mathf::Vector3& axis = Mathf::Vector3(0, 1, 0));

	void SetExplosiveEffect(float initialSpeed = 50.0f, float speedDecay = 2.0f, float randomFactor = 0.4f, float sphereRadius = 1.0f);

	VelocityMode GetVelocityMode() const { return m_velocityMode; }
	const std::vector<VelocityPoint>& GetVelocityCurve() const { return m_velocityCurve; }
	const std::vector<ImpulseData>& GetImpulses() const { return m_impulses; }
	const WindData& GetWindData() const { return m_windData; }
	const OrbitalData& GetOrbitalData() const { return m_orbitalData; }
    const ExplosiveData& GetExplosiveData() const { return m_explosiveData; }

	void ClearVelocityCurve();

	void ClearImpulses();

private:
	void UpdateConstantBuffers(float delta);
	void UpdateStructuredBuffers();


	// Parameters structure (constant buffer)
	struct alignas(16) MovementParams
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
	};

	VelocityMode m_velocityMode;
	std::vector<VelocityPoint> m_velocityCurve;
	std::vector<ImpulseData> m_impulses;
	WindData m_windData;
	OrbitalData m_orbitalData;
	ExplosiveData m_explosiveData;

	float m_currentTime;

private:
	// Basic movement properties
	bool m_gravity;
	float m_gravityStrength;
	bool m_easingEnabled;
	int m_easingType;

	// Compute shader related variables
	ID3D11ComputeShader* m_computeShader;
	ID3D11Buffer* m_movementParamsBuffer;

	// Structured buffers for velocity curve and impulses
	ID3D11Buffer* m_velocityCurveBuffer;
	ID3D11ShaderResourceView* m_velocityCurveSRV;
	ID3D11Buffer* m_impulsesBuffer;
	ID3D11ShaderResourceView* m_impulsesSRV;

	// State tracking
	bool m_paramsDirty;
	UINT m_particleCapacity = 0;
};