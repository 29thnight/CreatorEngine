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

struct alignas(16) EffectParameters		// 공통 effectparams
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
	LIFE,			// 생명 주기
	SPAWN,          // 파티클 생성
	SIMULATION,     // 물리 시뮬레이션
	MODIFICATION,   // 파티클 속성 변경
	RENDERING       // 렌더링 준비
};

// 3D 메시 파티클 데이터
struct alignas(16) MeshParticleData
{
	Mathf::Vector3 position;
	float pad1;

	Mathf::Vector3 velocity;
	float pad2;

	Mathf::Vector3 acceleration;
	float pad3;

	Mathf::Vector3 rotation;      // 3D 회전 (Euler angles)
	float pad4;

	Mathf::Vector3 rotationSpeed; // 3D 회전 속도
	float pad5;

	Mathf::Vector3 scale;         // 3D 스케일
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
	Constant,           // 일정한 속도
	Curve,             // 시간에 따른 곡선
	Impulse,           // 특정 시점에 충격
	Wind,              // 바람 효과
	Orbital,            // 궤도 운동
	Explosive,			// 폭발
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

struct ExplosiveData
{
	float initialSpeed;      // 초기 폭발 속도
	float speedDecay;        // 속도 감소율
	float randomFactor;      // 랜덤 요소 강도
	float sphereRadius;      // 구형 분포 반지름 (0이면 완전한 구)
};
