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

			// ��¡ �Լ� ����
			if (IsEasingEnabled())
			{
				// �θ� Ŭ������ ApplyEasing �޼��� ���
				normalizedAge = ApplyEasing(normalizedAge);
			}

			// ��¡�� ����� normalizedAge�� ���� ���
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
		return Mathf::Vector4(1, 1, 1, 1); // �⺻��

	default:
		return Mathf::Vector4(1, 1, 1, 1); // �⺻��
	}
}

Mathf::Vector4 ColorModule::EvaluateGradient(float t)
{
	// �׶��̼ǿ� ������ �� ��
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

	// t���� ���� �ش� �ε����� ���� ��ȯ
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
			// �浹 �˻�
			if (particle.position.y < m_floorHeight && particle.velocity.y < 0.0f)
			{
				// �ݻ�
				particle.position.y = m_floorHeight;
				particle.velocity.y = -particle.velocity.y * m_bounceFactor;

				// ������
				particle.velocity.x *= 0.9f;
				particle.velocity.z *= 0.9f;
			}
		}
	}
}



