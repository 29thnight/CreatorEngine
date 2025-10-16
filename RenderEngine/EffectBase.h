#pragma once
#include "ParticleSystem.h"
#include "SoundComponent.h"

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
    float dieDelay = 3.0f;
    float m_finishedTime = 0.0f;

    struct EmitterTiming {
        float startDelay = 0.0f;        // �� emitter�� ���۵Ǵ� �ð�
        bool hasStarted = false;         // ���� ����
    };
    std::vector<EmitterTiming> m_emitterTimings;

public:
    EffectBase() = default;
    virtual ~EffectBase() = default;

    // �ٽ� �������̽�
    virtual void Update(float delta) {
        if (m_state != EffectState::Playing && m_state != EffectState::Finished) return;

        m_currentTime += delta * m_timeScale;

        float progressRatio = 0.0f;
        bool isInfinite = (m_duration < 0);

        if (isInfinite) {
            progressRatio = std::fmod(m_currentTime, 1.0f);
        }
        else if (m_duration > 0) {
            progressRatio = std::clamp(m_currentTime / m_duration, 0.0f, 1.0f);
        }

        // Emitter�� ���� �ð� üũ �� ������Ʈ
        for (size_t i = 0; i < m_particleSystems.size(); ++i) {
            auto& ps = m_particleSystems[i];
            if (!ps) continue;

            // ���� ���� ���� emitter�� �ð� üũ
            if (!m_emitterTimings[i].hasStarted) {
                if (m_currentTime >= m_emitterTimings[i].startDelay) {
                    m_emitterTimings[i].hasStarted = true;
                    ps->Play();
                }
                else {
                    continue;  // ���� ���� �ð� �ȵ�
                }
            }

            ps->SetEffectProgress(progressRatio);
            ps->Update(delta * m_timeScale);
        }

        // duration�� ������ Finished ���·� ���� (Stop ���)
        if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
            if (m_state == EffectState::Playing) {
                m_state = EffectState::Finished;

                // �� ��ƼŬ ������ �ߴ�
                for (auto& ps : m_particleSystems) {
                    if (ps) {
                        ps->StopSpawning();
                    }
                }
            }
        }

        if (m_state == EffectState::Finished) {
            m_finishedTime += delta * m_timeScale;

            // 3�� �� �Ǵ� ��� ��ƼŬ�� ������� Stopped�� ��ȯ
            if (m_finishedTime >= dieDelay) {
                m_state = EffectState::Stopped;
                m_finishedTime = 0.0f;

                // ���۵� �͵鸸 ����
                for (size_t i = 0; i < m_particleSystems.size(); ++i) {
                    if (m_particleSystems[i] && m_emitterTimings[i].hasStarted) {
                        m_particleSystems[i]->Stop();
                        m_particleSystems[i]->ResumeSpawning();
                    }
                    m_emitterTimings[i].hasStarted = false;
                }
            }
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

        // ��� emitter�� ���� �÷��� ����
        for (auto& timing : m_emitterTimings) {
            timing.hasStarted = false;
        }

        // delay=0�� emitter�鸸 ��� ����
        for (size_t i = 0; i < m_particleSystems.size(); ++i) {
            if (m_emitterTimings[i].startDelay == 0.0f) {
                m_particleSystems[i]->Play();
                m_emitterTimings[i].hasStarted = true;
            }
        }
    }

    virtual void Stop() {
        m_state = EffectState::Stopped;
        m_currentTime = 0.0f;
        m_finishedTime = 0.0f;

        // ��� emitter ���� �� �÷��� ����
        for (size_t i = 0; i < m_particleSystems.size(); ++i) {
            if (m_particleSystems[i]) {
                if (m_emitterTimings[i].hasStarted) {
                    m_particleSystems[i]->Stop();
                    m_particleSystems[i]->ResumeSpawning();
                }
            }
            m_emitterTimings[i].hasStarted = false;
        }
    }

    virtual void Pause() {
        if (m_state == EffectState::Playing) {
            m_state = EffectState::Paused;
        }
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Pause();
            }
        }
    }

    virtual void Resume() {
        if (m_state == EffectState::Paused) {
            m_state = EffectState::Playing;
        }
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->Resume();
            }
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
                ps->UpdateEffectBaseRotation(newRotation);
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
        m_state = EffectState::Stopped;
        m_currentTime = 0.0f;
        m_position = Mathf::Vector3(0, 0, 0);
        m_rotation = Mathf::Vector3(0, 0, 0);

        // Timing ���� ����
        for (auto& timing : m_emitterTimings) {
            timing.hasStarted = false;
        }

        WaitForGPUCompletion();

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
        //for (auto& ps : m_particleSystems) {
        //    if (ps) {
        //        ps->WaitForGPUCompletion();
        //    }
        //}

        // ��ü GPU ���������� �÷���
        //if (DirectX11::DeviceStates->g_pDeviceContext) {
        //    DirectX11::DeviceStates->g_pDeviceContext->Flush();
        //
        //    // �߰� ����ȭ (�ʿ��� ���)
        //    //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        //}
    }

    const Mathf::Vector3& GetRotation() const { return m_rotation; }

    // ParticleSystem ����
    void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
        if (ps) {
            m_particleSystems.push_back(ps);

            // Timing ������ �Բ� �߰�
            EmitterTiming timing;
            timing.startDelay = 0.0f;
            timing.hasStarted = false;
            m_emitterTimings.push_back(timing);
        }
    }

    void RemoveParticleSystem(int index) {
        if (index >= 0 && index < m_particleSystems.size()) {
            m_particleSystems.erase(m_particleSystems.begin() + index);
        }
        if (index < m_emitterTimings.size()) {
            m_emitterTimings.erase(m_emitterTimings.begin() + index);
        }
    }

    void ClearParticleSystems() {
        m_particleSystems.clear();
        m_emitterTimings.clear();  // Timing�� �Բ� Ŭ����
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

    void SetEmitterStartDelay(int index, float delay) {
        if (index >= 0 && index < m_emitterTimings.size()) {
            m_emitterTimings[index].startDelay = delay;
        }
    }

    float GetEmitterStartDelay(int index) const {
        if (index >= 0 && index < m_emitterTimings.size()) {
            return m_emitterTimings[index].startDelay;
        }
        return 0.0f;
    }
};