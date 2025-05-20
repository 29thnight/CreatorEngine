#include "EffectModules.h"
#include "Camera.h"
#include "ShaderSystem.h"

void ColorModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			float normalizedAge = particle.age / particle.lifeTime;

			// 이징 함수 적용
			if (IsEasingEnabled())
			{
				// 부모 클래스의 ApplyEasing 메서드 사용
				normalizedAge = ApplyEasing(normalizedAge);
			}

			// 이징이 적용된 normalizedAge로 색상 계산
			particle.color = EvaluateColor(normalizedAge);
		}
	}
}

Mathf::Vector4 ColorModule::EvaluateColor(float t)
{
	switch (m_transitionMode)
	{
	case ColorTransitionMode::Gradient:
		return EvaluateGradient(t);

	case ColorTransitionMode::Discrete:
		return EvaluateDiscrete(t);

	case ColorTransitionMode::Custom:
		if (m_customEvaluator)
			return m_customEvaluator(t);
		return Mathf::Vector4(1, 1, 1, 1); // 기본값

	default:
		return Mathf::Vector4(1, 1, 1, 1); // 기본값
	}
}

Mathf::Vector4 ColorModule::EvaluateGradient(float t)
{
	// 그라데이션에 적절한 색 평가
	if (t <= m_colorGradient[0].first)
		return m_colorGradient[0].second;

	if (t >= m_colorGradient.back().first)
		return m_colorGradient.back().second;

	for (size_t i = 0; i < m_colorGradient.size() - 1; i++)
	{
		if (t >= m_colorGradient[i].first && t <= m_colorGradient[i + 1].first)
		{
			float localT = (t - m_colorGradient[i].first) /
				(m_colorGradient[i + 1].first - m_colorGradient[i].first);

			return Mathf::Vector4::Lerp(
				m_colorGradient[i].second,
				m_colorGradient[i + 1].second,
				localT
			);
		}
	}
	return m_colorGradient[0].second;
}

Mathf::Vector4 ColorModule::EvaluateDiscrete(float t)
{
	if (m_discreteColors.empty())
		return Mathf::Vector4(1, 1, 1, 1);

	// t값에 따라 해당 인덱스의 색상 반환
	size_t index = static_cast<size_t>(t * m_discreteColors.size());
	index = std::min(index, m_discreteColors.size() - 1);
	return m_discreteColors[index];
}

void SizeModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			float normalizedAge = particle.age / particle.lifeTime;
			if (IsEasingEnabled())
			{
				normalizedAge = ApplyEasing(normalizedAge);
			}
			particle.size = m_sizeOverLife(normalizedAge);
		}
	}
}

void CollisionModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			// 충돌 검사
			if (particle.position.y < m_floorHeight && particle.velocity.y < 0.0f)
			{
				// 반사
				particle.position.y = m_floorHeight;
				particle.velocity.y = -particle.velocity.y * m_bounceFactor;

				// 마찰력
				particle.velocity.x *= 0.9f;
				particle.velocity.z *= 0.9f;
			}
		}
	}
}



