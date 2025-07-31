#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

enum class VelocityMode
{
	Constant,           // 일정한 속도
	Curve,             // 시간에 따른 곡선
	Impulse,           // 특정 시점에 충격
	Wind,              // 바람 효과
	Orbital            // 궤도 운동
};

struct alignas(16) VelocityPoint
{
	float time;
	Mathf::Vector3 velocity;
	float strength;
	float3 pad1;
};

struct alignas(16) ImpulseData
{
	float triggerTime;
	Mathf::Vector3 direction;
	float force;
	float duration;
	float2 pad1;
};

struct WindData
{
	Mathf::Vector3 direction;
	float baseStrength;
	float turbulence;        // 난기류 강도
	float frequency;         // 변화 빈도
};

struct OrbitalData
{
	Mathf::Vector3 center;
	float radius;
	float speed;
	Mathf::Vector3 axis;     // 회전축
};

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

	virtual void ResetForReuse();
	virtual bool IsReadyForReuse() const;

	// Movement settings
	void SetUseGravity(bool use) { m_gravity = use; m_paramsDirty = true; }

	bool GetUseGravity() { return m_gravity; }

	void SetGravityStrength(float strength) { m_gravityStrength = strength; m_paramsDirty = true; }
	void SetEasingEnabled(bool enabled) { m_easingEnabled = enabled; m_paramsDirty = true; }
	void SetEasingType(int type) { m_easingType = type; m_paramsDirty = true; }

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

	VelocityMode GetVelocityMode() const { return m_velocityMode; }
	const std::vector<VelocityPoint>& GetVelocityCurve() const { return m_velocityCurve; }
	const std::vector<ImpulseData>& GetImpulses() const { return m_impulses; }
	const WindData& GetWindData() const { return m_windData; }
	const OrbitalData& GetOrbitalData() const { return m_orbitalData; }

	void ClearVelocityCurve();

	void ClearImpulses();

private:
	void UpdateConstantBuffers(float delta);
	void UpdateStructuredBuffers();
	void Release();

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

		int velocityCurveSize;
		int impulseCount;
		float2 pad2;
	};

	VelocityMode m_velocityMode;
	std::vector<VelocityPoint> m_velocityCurve;
	std::vector<ImpulseData> m_impulses;
	WindData m_windData;
	OrbitalData m_orbitalData;
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