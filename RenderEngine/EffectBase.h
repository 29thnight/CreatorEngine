#pragma once
#include "ParticleSystem.h"

// Effect�� �⺻ �������̽�
class EffectBase {
protected:
    // ���� ParticleSystem���� ����
    std::vector<std::shared_ptr<ParticleSystem>> m_particleSystems;

    // Effect �⺻ ����
    std::string m_name;
    Mathf::Vector3 m_position;
    bool m_isPlaying = false;
    bool m_isPaused = false;

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
        if (!m_isPlaying || m_isPaused) return;

        // �ð� ������Ʈ
        m_currentTime += delta * m_timeScale;

        // ���ӽð� üũ
        if (m_duration > 0 && m_currentTime >= m_duration) {
            if (m_loop) {
                m_currentTime = 0.0f;
            }
            else {
                Stop();
                return;
            }
        }

        // ��� ParticleSystem ������Ʈ
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Update(delta * m_timeScale);
            }
        }
    }

    virtual void Render(RenderScene& scene, Camera& camera) {
        if (!m_isPlaying) return;

        // ��� ParticleSystem ������
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Render(scene, camera);
            }
        }
    }

    // Effect ����
    virtual void Play() {
        m_isPlaying = true;
        m_isPaused = false;
        m_currentTime = 0.0f;

        // ��� ParticleSystem ���
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

        // ��� ParticleSystem ����
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

    // ��ġ ���� (��� ParticleSystem�� ����)
    virtual void SetPosition(const Mathf::Vector3& position) {
        m_position = position;

        // ��� ParticleSystem�� ���� ��ġ ���� ���� (offset ������� ����)
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->SetPosition(position);  // ���� ��ġ ���� ����
            }
        }
    }

    // ParticleSystem ����
    void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
        if (ps) {
            m_particleSystems.push_back(ps);
            // ���� Effect ���¿� �°� ParticleSystem ����
            if (m_isPlaying && !m_isPaused) {
                ps->Play();
            }
            ps->SetPosition(m_position);
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
