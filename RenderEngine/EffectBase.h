#pragma once
#include "ParticleSystem.h"

enum class EffectState {
	Stopped,    // 정지됨 (렌더링 안함)
	Playing,    // 재생 중
	Paused,     // 일시정지
	Finished    // 종료됨 (렌더링은 함)
};

// Effect의 기본 인터페이스
class EffectBase {
protected:
	// 여러 ParticleSystem들을 관리
	std::vector<std::shared_ptr<ParticleSystem>> m_particleSystems;

	// Effect 기본 정보
	std::string m_name;
	Mathf::Vector3 m_position = Mathf::Vector3(0, 0, 0);
	Mathf::Vector3 m_rotation = Mathf::Vector3(0, 0, 0);
	EffectState m_state = EffectState::Stopped;

	// Effect 전체 설정
	float m_timeScale = 1.0f;
	bool m_loop = true;
	float m_duration = -1.0f;  // -1이면 무한
	float m_currentTime = 0.0f;

public:
	EffectBase() = default;
	virtual ~EffectBase() = default;

	// 핵심 인터페이스
	virtual void Update(float delta) {
		// 위치 업데이트는 항상 수행 (상태와 무관)
		UpdateTransform();

		// 파티클/렌더링 업데이트는 상태에 따라 수행
		if (m_state == EffectState::Playing) {
			UpdatePlayback(delta);
		}
		else {
			// Playing 상태가 아니어도 ParticleSystem의 위치는 업데이트
			UpdatePositionOnly();
		}
	}

	virtual void Render(RenderScene& scene, Camera& camera) {
		// Stopped 상태일 때만 렌더링 안함
		if (m_state == EffectState::Stopped) return;

		// 모든 ParticleSystem 렌더링
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->Render(scene, camera);
			}
		}
	}

	// Effect 제어
	virtual void Play() {
		m_state = EffectState::Playing;
		m_currentTime = 0.0f;

		// 모든 ParticleSystem 재생
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->Play();
			}
		}
	}

	virtual void Stop() {
		m_state = EffectState::Stopped;
		m_currentTime = 0.0f;

		// 모든 ParticleSystem 정지
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

	// 위치 설정 (모든 ParticleSystem에 적용)
	virtual void SetPosition(const Mathf::Vector3& newBasePosition) {
		m_position = newBasePosition;

		// 모든 ParticleSystem을 변화량만큼 이동 (상대 위치 관계 유지)
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->UpdateEffectBasePosition(newBasePosition);
			}
		}
	}

	virtual void SetRotation(const Mathf::Vector3& newRotation) {
		m_rotation = newRotation;

		// 모든 ParticleSystem에 회전 적용
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->SetRotation(m_rotation);
			}
		}
	}

	const Mathf::Vector3& GetRotation() const { return m_rotation; }

	// ParticleSystem 관리
	void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
		if (ps) {
			m_particleSystems.push_back(ps);
			// 현재 Effect 상태에 맞게 ParticleSystem 설정
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
		// 위치/회전 변경사항 감지 및 적용
		// 이 부분은 항상 실행되어야 함
		for (auto& ps : m_particleSystems) {
			if (ps) {
				// 위치 관련 업데이트만 수행
				ps->UpdateTransformOnly();
			}
		}
	}
	
	void UpdatePlayback(float delta) {
		// 시간 업데이트
		m_currentTime += delta * m_timeScale;

		// 진행률 계산
		float progressRatio = 0.0f;
		bool isInfinite = (m_duration < 0);
		bool shouldRender = true;

		if (isInfinite) {
			// 무한 재생 모드
			progressRatio = (std::sin(m_currentTime) + 1.0f) * 0.5f;
			shouldRender = true;
		}
		else if (m_duration > 0) {
			progressRatio = std::clamp(m_currentTime / m_duration, 0.0f, 1.0f);
			shouldRender = (m_currentTime <= m_duration);
		}

		// ParticleSystem 업데이트 (렌더링 포함)
		for (auto& ps : m_particleSystems) {
			if (ps) {
				ps->SetEffectProgress(progressRatio);
				ps->SetShouldRender(shouldRender);  // 렌더링 여부 설정
				ps->Update(delta * m_timeScale);
			}
		}

		// 종료 처리 (무한 재생이 아닌 경우에만)
		if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
			if (m_loop) {
				m_currentTime = 0.0f;
				// 루프 시에는 파티클을 재시작
				for (auto& ps : m_particleSystems) {
					if (ps) {
						ps->Stop();
						ps->Play();
					}
				}
			}
			else {
				// 재생은 멈추지만 객체는 유지 (위치 업데이트는 계속됨)
				m_state = EffectState::Stopped;

				// 파티클 생성은 중단하지만 기존 파티클은 유지
				for (auto& ps : m_particleSystems) {
					if (ps) {
						ps->StopSpawning();  // 새로운 파티클 생성만 중단
					}
				}
			}
		}
	}

	void UpdatePositionOnly() {
		// Playing 상태가 아닐 때도 위치는 업데이트
		for (auto& ps : m_particleSystems) {
			if (ps) {
				// 위치/Transform만 업데이트, 파티클 생성/시뮬레이션은 안 함
				ps->UpdateTransformOnly();
			}
		}
	}

};