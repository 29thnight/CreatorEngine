#pragma once
#include "ParticleSystem.h"

// Effect의 기본 인터페이스
class EffectBase {
protected:
    // 여러 ParticleSystem들을 관리
    std::vector<std::shared_ptr<ParticleSystem>> m_particleSystems;

    // Effect 기본 정보
    std::string m_name;
    Mathf::Vector3 m_position = Mathf::Vector3(0, 0, 0);
    bool m_isPlaying = false;
    bool m_isPaused = false;

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
        if (!m_isPlaying || m_isPaused) return;

        // 시간 업데이트
        m_currentTime += delta * m_timeScale;

        // 지속시간 체크
        if (m_duration > 0 && m_currentTime >= m_duration) {
            if (m_loop) {
                m_currentTime = 0.0f;
            }
            else {
                Stop();
                return;
            }
        }

        // 모든 ParticleSystem 업데이트
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Update(delta * m_timeScale);
            }
        }
    }

    virtual void Render(RenderScene& scene, Camera& camera) {
        if (!m_isPlaying) return;

        // 모든 ParticleSystem 렌더링
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Render(scene, camera);
            }
        }
    }

    // Effect 제어
    virtual void Play() {
        m_isPlaying = true;
        m_isPaused = false;
        m_currentTime = 0.0f;

        // 모든 ParticleSystem 재생
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Play();
            }
        }
    }

    virtual void Stop() {
        m_isPlaying = false;
        m_isPaused = false;
        m_currentTime = 0.0f;

        // 모든 ParticleSystem 정지
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Stop();
            }
        }
    }

    virtual void Pause() {
        m_isPaused = true;
    }

    virtual void Resume() {
        m_isPaused = false;
    }

    // 위치 설정 (모든 ParticleSystem에 적용)
    virtual void SetPosition(const Mathf::Vector3& newBasePosition) {
        // 이전 기준점에서 새 기준점으로의 변화량 계산
        Mathf::Vector3 deltaPosition = newBasePosition - m_position;

        // 기준점 업데이트
        m_position = newBasePosition;

        // 모든 ParticleSystem을 변화량만큼 이동 (상대 위치 관계 유지)
        for (auto& ps : m_particleSystems) {
            if (ps) {
                Mathf::Vector3 currentPos = ps->GetPosition();
                ps->SetPosition(currentPos + deltaPosition);
            }
        }
    }

    // ParticleSystem 관리
    void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
        if (ps) {
            m_particleSystems.push_back(ps);
            // 현재 Effect 상태에 맞게 ParticleSystem 설정
            if (m_isPlaying && !m_isPaused) {
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

    bool IsPlaying() const { return m_isPlaying; }
    bool IsPaused() const { return m_isPaused; }

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
};
