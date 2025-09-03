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
    Mathf::Vector3 m_scale = Mathf::Vector3(0, 0, 0);
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
        if (m_state != EffectState::Playing) return;

        // �ð� ������Ʈ
        m_currentTime += delta * m_timeScale;

        // ����� ���
        float progressRatio = 0.0f;
        bool isInfinite = (m_duration < 0);

        if (isInfinite) {
            // ���� ��� ��� - ������ �ֱ�� 0~1 �ݺ� (��: 1�� �ֱ�)
            progressRatio = std::fmod(m_currentTime, 1.0f);
        }
        else if (m_duration > 0) {
            progressRatio = std::clamp(m_currentTime / m_duration, 0.0f, 1.0f);
        }

        // ParticleSystem ������Ʈ
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->SetEffectProgress(progressRatio);
                ps->Update(delta * m_timeScale);
            }
        }

        // duration�� ������ �ٷ� Stop
        if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
            Stop();
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

    virtual void SetScale(const Mathf::Vector3& newScale) {
        m_scale = newScale;

        // ��� ParticleSystem�� ȸ�� ����
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->SetScale(m_scale);
            }
        }
    }

    // Ǯ���� ���� ���� ����
    virtual void ResetForReuse() {
        // ���� ���� ����
        m_state = EffectState::Stopped;
        m_currentTime = 0.0f;

        // ��ġ/ȸ�� �ʱ�ȭ
        m_position = Mathf::Vector3(0, 0, 0);
        m_rotation = Mathf::Vector3(0, 0, 0);

        // GPU �۾� �Ϸ� ���
        WaitForGPUCompletion();

        // ParticleSystem�� ����
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->ResetForReuse();
            }
        }
    }


    // ���� �غ� �Ϸ� ���� üũ
    virtual bool IsReadyForReuse() const {
        // ���� ���°� �ƴϸ� ���� �Ұ�
        if (m_state != EffectState::Stopped) {
            return false;
        }

        // ParticleSystem���� ���� �غ� �Ǿ����� Ȯ��
        for (const auto& ps : m_particleSystems) {
            if (ps) {
                // GPU �۾��� �Ϸ���� �ʾ����� ��� ���
                if (!ps->IsReadyForReuse()) {
                    // �� �� �� ��ȸ�� �ֱ� ���� GPU �Ϸ� ���
                    ps->WaitForGPUCompletion();

                    // �ٽ� �� �� Ȯ��
                    if (!ps->IsReadyForReuse()) {
                        return false;
                    }
                }
            }
        }
        return true;
    }


    // GPU �۾� �Ϸ� ���
    void WaitForGPUCompletion() {
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->WaitForGPUCompletion();
            }
        }

        // ��ü GPU ���������� �÷���
        if (DirectX11::DeviceStates->g_pDeviceContext) {
            DirectX11::DeviceStates->g_pDeviceContext->Flush();

            // �߰� ����ȭ (�ʿ��� ���)
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
};