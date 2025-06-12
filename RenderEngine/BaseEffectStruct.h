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