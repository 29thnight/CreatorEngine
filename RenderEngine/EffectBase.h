#pragma once
#include "ParticleSystem.h"

enum class EffectState {
	Stopped,    // ������ (������ ����)
	Playing,    // ��� ��
	Paused,     // �Ͻ�����
	Finished    // ����� (�������� ��)
};

// Effect�� �⺻ �������̽�
class EffectBase {
protected:
	// ���� ParticleSystem���� ����
	std::vector<std::shared_ptr<ParticleSystem>> m_particleSystems;

	// Effect �⺻ ����
	std::string m_name;
	Mathf::Vector3 m_position = Mathf::Vector3(0, 0, 0);
	Mathf::Vector3 m_rotation = Mathf::Vector3(0, 0, 0);
	EffectState m_state = EffectState::Stopped;

	// Effect ��ü ����
	float m_timeScale = 1.0f;
	bool m_loop = true;
	float m_duration = -1.0f;  // -1�̸� ����
	float m_currentTime = 0.0f;

public:
	EffectBase() = default;
	virtual ~EffectBase() = default;

	// �ٽ� �������̽�
	virtual void Update(float delta) {
		// ��ġ ������Ʈ�� �׻� ���� (���¿� ����)
		UpdateTransform();

		// ��ƼŬ/������ ������Ʈ�� ���¿� ���� ����
		if (m_state == EffectState::Playing) {
			UpdatePlayback(delta);
		}
		else {
			// Playing ���°� �ƴϾ ParticleSystem�� ��ġ�� ������Ʈ
			UpdatePositionOnly();
		}
	}

	virtual void Render(RenderScene& scene, Camera& camera) {
		// Stopped ������ ���� ������ ����
		if (m_state == EffectState::Stopped) return;

		// ��� ParticleSystem ������
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->Render(scene, camera);
			}
		}
	}

	// Effect ����
	virtual void Play() {
		m_state = EffectState::Playing;
		m_currentTime = 0.0f;

		// ��� ParticleSystem ���
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->Play();
			}
		}
	}

	virtual void Stop() {
		m_state = EffectState::Stopped;
		m_currentTime = 0.0f;

		// ��� ParticleSystem ����
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->Stop();
			}
		}
	}

	virtual void Pause() {
		if (m_state == EffectState::Playing) {
			m_state = EffectState::Paused;
		}
	}

	virtual void Resume() {
		if (m_state == EffectState::Paused) {
			m_state = EffectState::Playing;
		}
	}

	// ��ġ ���� (��� ParticleSystem�� ����)
	virtual void SetPosition(const Mathf::Vector3& newBasePosition) {
		m_position = newBasePosition;

		// ��� ParticleSystem�� ��ȭ����ŭ �̵� (��� ��ġ ���� ����)
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->UpdateEffectBasePosition(newBasePosition);
			}
		}
	}

	virtual void SetRotation(const Mathf::Vector3& newRotation) {
		m_rotation = newRotation;

		// ��� ParticleSystem�� ȸ�� ����
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->SetRotation(m_rotation);
			}
		}
	}

	const Mathf::Vector3& GetRotation() const { return m_rotation; }

	// ParticleSystem ����
	void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
		if (ps) {
			m_particleSystems.push_back(ps);
			// ���� Effect ���¿� �°� ParticleSystem ����
			if (m_state == EffectState::Playing) {
				ps->Play();
			}
		}
	}

	void RemoveParticleSystem(int index) {
		if (index >= 0 && index < m_particleSystems.size()) {
			m_particleSystems.erase(m_particleSystems.begin() + index);
		}
	}

	void ClearParticleSystems() {
		m_particleSystems.clear();
	}

	// Getter/Setter
	const std::string& GetName() const { return m_name; }
	void SetName(const std::string& name) { m_name = name; }

	const Mathf::Vector3& GetPosition() const { return m_position; }

	bool IsPlaying() const { return m_state == EffectState::Playing; }
	bool IsPaused() const { return m_state == EffectState::Paused; }
	bool IsFinished() const { return m_state == EffectState::Finished; }
	EffectState GetState() const { return m_state; }

	float GetTimeScale() const { return m_timeScale; }
	void SetTimeScale(float scale) { m_timeScale = scale; }

	float GetDuration() const { return m_duration; }
	void SetDuration(float duration) { m_duration = duration; }

	bool IsLooping() const { return m_loop; }
	void SetLoop(bool loop) { m_loop = loop; }

	float GetCurrentTime() const { return m_currentTime; }

	size_t GetParticleSystemCount() const { return m_particleSystems.size(); }
	std::shared_ptr<ParticleSystem> GetParticleSystem(int index) const {
		if (index >= 0 && index < m_particleSystems.size()) {
			return m_particleSystems[index];
		}
		return nullptr;
	}

	const std::vector<std::shared_ptr<ParticleSystem>>& GetAllParticleSystems() const {
		return m_particleSystems;
	}


private:
	void UpdateTransform() {
		// ��ġ/ȸ�� ������� ���� �� ����
		// �� �κ��� �׻� ����Ǿ�� ��
		for (auto& ps : m_particleSystems) {
			if (ps) {
				// ��ġ ���� ������Ʈ�� ����
				ps->UpdateTransformOnly();
			}
		}
	}
	
	void UpdatePlayback(float delta) {
		// �ð� ������Ʈ
		m_currentTime += delta * m_timeScale;

		// ����� ���
		float progressRatio = 0.0f;
		bool isInfinite = (m_duration < 0);
		bool shouldRender = true;

		if (isInfinite) {
			// ���� ��� ���
			progressRatio = (std::sin(m_currentTime) + 1.0f) * 0.5f;
			shouldRender = true;
		}
		else if (m_duration > 0) {
			progressRatio = std::clamp(m_currentTime / m_duration, 0.0f, 1.0f);
			shouldRender = (m_currentTime <= m_duration);
		}

		// ParticleSystem ������Ʈ (������ ����)
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->SetEffectProgress(progressRatio);
				ps->SetShouldRender(shouldRender);  // ������ ���� ����
				ps->Update(delta * m_timeScale);
			}
		}

		// ���� ó�� (���� ����� �ƴ� ��쿡��)
		if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
			if (m_loop) {
				m_currentTime = 0.0f;
				// ���� �ÿ��� ��ƼŬ�� �����
				for (auto& ps : m_particleSystems) {
					if (ps) {
						ps->Stop();
						ps->Play();
					}
				}
			}
			else {
				// ����� �������� ��ü�� ���� (��ġ ������Ʈ�� ��ӵ�)
				m_state = EffectState::Stopped;

				// ��ƼŬ ������ �ߴ������� ���� ��ƼŬ�� ����
				for (auto& ps : m_particleSystems) {
					if (ps) {
						ps->StopSpawning();  // ���ο� ��ƼŬ ������ �ߴ�
					}
				}
			}
		}
	}

	void UpdatePositionOnly() {
		// Playing ���°� �ƴ� ���� ��ġ�� ������Ʈ
		for (auto& ps : m_particleSystems) {
			if (ps) {
				// ��ġ/Transform�� ������Ʈ, ��ƼŬ ����/�ùķ��̼��� �� ��
				ps->UpdateTransformOnly();
			}
		}
	}

};