#include "EffectModules.h"
#include "Camera.h"
#include "ShaderSystem.h"

void SpawnModule::Initialize()
{
	m_uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);

	m_particleTemplate.age = 0.0f;
	m_particleTemplate.lifeTime = 1.0f;
	m_particleTemplate.rotation = 0.0f;
	m_particleTemplate.rotatespeed = 1.0f;
	m_particleTemplate.size = float2(1.0f, 1.0f);
	m_particleTemplate.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
	m_particleTemplate.velocity = float3(0.0f, 0.0f, 0.0f);
}

void SpawnModule::Update(float delta, std::vector<ParticleData>& particles)
{
	m_Time += delta;

	float spawnInterval = 1.0f / m_spawnRate;
	while (m_Time >= spawnInterval)
	{
		SpawnParticle(particles);
		m_Time -= spawnInterval;
	}

}

void SpawnModule::SpawnParticle(std::vector<ParticleData>& particles)
{
	// 비활성 파티클 재활성화
	for (auto& particle : particles)
	{
		if (!particle.isActive)
		{
			InitializeParticle(particle);
			particle.isActive = true;
			break;
		}
	}
}

void SpawnModule::InitializeParticle(ParticleData& particle)
{
	particle = m_particleTemplate;

	switch (m_emitterType)
	{
	case EmitterType::point:
		particle.position = float3(0.0f, 0.0f, 0.0f);
		break;
	case EmitterType::sphere:
	{
		float theta = m_uniform(m_random) * 6.28f;  // 0 ~ 2π (방위각)
		float phi = m_uniform(m_random) * 3.14f;    // 0 ~ π (고도각)
		float radius = 1.0f;                        // 반지름
		particle.position = float3(
			radius * sin(phi) * cos(theta),
			radius * sin(phi) * sin(theta),  // 수정된 부분
			radius * cos(phi)
		);
		break;
	}
	case EmitterType::box:
	{
		particle.position = Mathf::Vector3(
			(m_uniform(m_random) - 0.5f) * 2.0f,
			(m_uniform(m_random) - 0.5f) * 2.0f,
			(m_uniform(m_random) - 0.5f) * 2.0f
		);
		break;
	}
	case EmitterType::cone:
	{
		float theta = m_uniform(m_random) * 6.28f;
		float height = m_uniform(m_random) * 1.0f;
		float radiusAtHeight = 0.5f * (1.0f - height);

		particle.position = Mathf::Vector3(
			radiusAtHeight * cos(theta),
			height,  // y축이 원뿔의 높이 방향
			radiusAtHeight * sin(theta)
		);
		break;
	}
	case EmitterType::circle:
	{
		float theta = m_uniform(m_random) * 6.28f;
		float radius = 0.5f + m_uniform(m_random) * 0.5f;
		particle.position = Mathf::Vector3(
			radius * cos(theta),
			0.0f,
			radius * sin(theta)
		);
		break;
	}
	}


	// 아래로 가속 (중력)
	particle.acceleration = float3(0.0f, -9.8f, 0.0f);
}

void MovementModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			if (m_gravity)
			{
				// 중력 적용 시 가속도 추가
				particle.velocity += particle.acceleration * m_gravityStrength * delta;
			}

			particle.position += particle.velocity * delta;

			particle.rotation += particle.rotatespeed * delta;
		}
	}
}

void LifeModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			particle.age += delta;

			if (particle.lifeTime > 0.0f && particle.age >= particle.lifeTime)
			{
				particle.isActive = false;
			}
		}
	}
}

void ColorModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			float normalizedAge = particle.age / particle.lifeTime;
			particle.color = EvaluateGradient(normalizedAge);
		}
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

void SizeModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			float normalizedAge = particle.age / particle.lifeTime;
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

EffectModules::EffectModules(int maxParticles) : m_maxParticles(maxParticles), m_isRunning(false)
{
	m_particleData.resize(maxParticles);
	for (auto& particle : m_particleData)
	{
		particle.isActive = false;
	}

	m_instanceData.resize(maxParticles);
}

EffectModules::~EffectModules()
{
	for (auto& module : m_modules)
	{
		delete module;
	}
	m_modules.clear();
}

void EffectModules::Play()
{
	m_isRunning = true;

	m_activeParticleCount = 0;

	for (auto& particle : m_particleData)
	{
		particle.isActive = false;
	}
}

void EffectModules::Update(float delta)
{
	if (!m_isRunning) // 완전히 정지된 상태
		return;

	if (m_isPaused) // 일시 정지 상태
	{
		m_activeParticleCount = 0;
		for (const auto& particle : m_particleData)
		{
			if (particle.isActive)
			{
				m_activeParticleCount++;
			}
		}
		return;
	}

	std::cout << m_activeParticleCount << std::endl;

	// 정상 실행 상태
	for (auto* module : m_modules)
	{
		module->Update(delta, m_particleData);
	}

	m_activeParticleCount = 0;
	for (const auto& particle : m_particleData)
	{
		if (particle.isActive)
		{
			m_activeParticleCount++;
		}
	}
}

void EffectModules::Render(RenderScene& scene, Camera& camera)
{
	if (!m_isRunning || m_activeParticleCount == 0)
		return;

	auto& deviceContext = DeviceState::g_pDeviceContext;

	//SaveRenderState();

	Mathf::Matrix world = XMMatrixIdentity();
	Mathf::Matrix view = XMMatrixTranspose(camera.CalculateView());
	Mathf::Matrix projection = XMMatrixTranspose(camera.CalculateProjection());

    //SetupBillBoardInstancing(m_instanceData.data(), m_activeParticleCount);

	for (auto* renderModule : m_renderModules)
	{
		if (m_activeParticleCount > 0)
		{
			// 인스턴스 데이터 설정
			renderModule->SetupInstancing(m_instanceData.data(), m_activeParticleCount);

			// PSO 적용
			renderModule->GetPSO()->Apply();

			// 렌더링 수행
			renderModule->Render(world, view, projection);
		}
	}

}

void EffectModules::SetPosition(const Mathf::Vector3& position)
{
	m_position = position;
	// SpawnModule은 위치를 직접 저장하지 않으므로, 모든 파티클의 위치를 이동
	for (auto& particle : m_particleData) {
		if (particle.isActive) {
			// 기존 위치에서 새 위치로의 오프셋 적용
			particle.position += position - m_position;
		}
	}
}


