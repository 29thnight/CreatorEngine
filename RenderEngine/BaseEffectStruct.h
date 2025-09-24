#pragma once
struct alignas(16) ParticleData
{
	Mathf::Vector3 position;
	float pad1;

	Mathf::Vector3 velocity;
	float pad2;

	Mathf::Vector3 acceleration;
	float pad3;

	Mathf::Vector2 size;
	float age;
	float lifeTime;

	float rotation;
	float rotatespeed;
	float2 pad4;

	Mathf::Vector4 color;

	UINT isActive;
	float3 pad5;
};

struct alignas(16) EffectParameters		// ���� effectparams
{
	float time;
	float intensity;
	float speed;
	float pad;
};


enum class EmitterType
{
	point,
	sphere,
	box,
	cone,
	circle,
};

enum class ColorTransitionMode
{
	Gradient,
	Discrete,
	Custom,
};

enum class ModuleStage {
	LIFE,			// ���� �ֱ�
	SPAWN,          // ��ƼŬ ����
	SIMULATION,     // ���� �ùķ��̼�
	MODIFICATION,   // ��ƼŬ �Ӽ� ����
	RENDERING       // ������ �غ�
};

// 3D �޽� ��ƼŬ ������
struct alignas(16) MeshParticleData
{
	Mathf::Vector3 position;
	float pad1;

	Mathf::Vector3 velocity;
	float pad2;

	Mathf::Vector3 acceleration;
	float pad3;

	Mathf::Vector3 rotation;      // 3D ȸ�� (Euler angles)
	float pad4;

	Mathf::Vector3 rotationSpeed; // 3D ȸ�� �ӵ�
	float pad5;

	Mathf::Vector3 scale;         // 3D ������
	float pad6;

	float age;
	float lifeTime;
	uint32_t isActive;
	uint32_t renderMode;

	Mathf::Vector4 color;

	uint32_t textureIndex;
	Mathf::Vector3 pad8;
};

struct alignas(16) SpawnParams
{
	float spawnRate;
	float deltaTime;
	float currentTime;
	int emitterType;

	float3 emitterSize;
	float emitterRadius;

	UINT maxParticles;
	Mathf::Vector3 emitterPosition;

	Mathf::Vector3 previousEmitterPosition;
	UINT forcePositionUpdate;

	Mathf::Vector3 emitterRotation;
	UINT forceRotationUpdate;

	Mathf::Vector3 previousEmitterRotation;
	UINT allowNewSpawn;
};

enum class VelocityMode
{
	Constant,           // ������ �ӵ�
	Curve,             // �ð��� ���� �
	Impulse,           // Ư�� ������ ���
	Wind,              // �ٶ� ȿ��
	Orbital,            // �˵� �
	Explosive,			// ����
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
	float impulseRange;
	UINT impulseType;
};

struct WindData
{
	Mathf::Vector3 direction;
	float baseStrength;
	float turbulence;        // ����� ����
	float frequency;         // ��ȭ ��
};

struct OrbitalData
{
	Mathf::Vector3 center;
	float radius;
	float speed;
	Mathf::Vector3 axis;     // ȸ����
};

struct ExplosiveData
{
	float initialSpeed;      // �ʱ� ���� �ӵ�
	float speedDecay;        // �ӵ� ������
	float randomFactor;      // ���� ��� ����
	float sphereRadius;      // ���� ���� ������ (0�̸� ������ ��)
};
