#pragma once
#include "Texture.h"
#include "RenderModules.h"
#include "IRenderPass.h"
#include "SpawnModuleCS.h"


// colorgradient
class ColorModule : public ParticleModule
{
private:
	using ColorEvaluator = std::function<Mathf::Vector4(float)>;
	ColorEvaluator m_customEvaluator = nullptr;
public:
	ColorModule()
	{
		m_colorGradient = {
			{0.0f, float4(1.0f,1.0f,1.0f,1.0f)},
			{0.3f, float4(1.0f,1.0f,0.0f,0.9f)},
			{0.7f, float4(1.0f,0.0f,0.0f,0.7f)},
			{1.0f, float4(0.5f,0.0f,0.0f,0.0f)},
		};

	}

	void Update(float delta, std::vector<ParticleData>& particles) override;

	// 보간
	void SetColorGradient(const std::vector<std::pair<float, Mathf::Vector4>>& gradient) { m_colorGradient = gradient; }

	// 모드 설정 (보간, 이산, 사용자 정의 함수)
	void SetTransitionMode(ColorTransitionMode mode) { m_transitionMode = mode; }

	// 사용자 정의 함수
	void SetCustomEvaluator(const ColorEvaluator& evaluator) { m_customEvaluator = evaluator; }

	// 이산
	void SetDiscreteColors(const std::vector<Mathf::Vector4>& colors) { m_discreteColors = colors; }
private:
	// 통합 색상 평가 함수
	Mathf::Vector4 EvaluateColor(float t);

	// 보간용 평가 함수
	Mathf::Vector4 EvaluateGradient(float t);

	// 이산용 평가 함수
	Mathf::Vector4 EvaluateDiscrete(float t);

	std::vector<std::pair<float, Mathf::Vector4>> m_colorGradient;

	ColorTransitionMode m_transitionMode = ColorTransitionMode::Gradient;

	std::vector<Mathf::Vector4> m_discreteColors;
};

// startsize, endsize, sizefunction
class SizeModule : public ParticleModule
{
public:
	SizeModule() : m_startSize(0.1f), m_endSize(1.0f), m_sizeOverLife([this](float t) {return Mathf::Vector2::Lerp(
		Mathf::Vector2(m_startSize, m_startSize),
		Mathf::Vector2(m_endSize, m_endSize),
		t
	);
		}) {}

	void Update(float delta, std::vector<ParticleData>& particles) override;

	// m_sizeoverlife function을 설정 안했을때의 기본으로 쓰이는 크기
	void SetStartSize(float size) { m_startSize = size; }
	void SetEndSize(float size) { m_endSize = size; }

	void SetSizeOverLifeFunction(std::function<Mathf::Vector2(float)> func) { m_sizeOverLife = func; }

private:
	float m_startSize;
	float m_endSize;
	std::function<Mathf::Vector2(float)> m_sizeOverLife;
};

// floorheight, bouncefactor 
class CollisionModule : public ParticleModule
{
public:
	CollisionModule() : m_floorHeight(0.0f), m_bounceFactor(0.5f) {}

	void Update(float delta, std::vector<ParticleData>& particles) override;

	void SetFloorHeight(float height) { m_floorHeight = height; }

	void SetBounceFactor(float factor) { m_bounceFactor = factor; }

private:
	float m_floorHeight;
	float m_bounceFactor;
};
