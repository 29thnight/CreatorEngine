#pragma once
#include "ParticleSystem.h"
#include "SoundComponent.h"

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
    Mathf::Vector3 m_scale = Mathf::Vector3(0, 0, 0);
    EffectState m_state = EffectState::Stopped;

    // Effect 전체 설정
    float m_timeScale = 1.0f;
    bool m_loop = true;
    float m_duration = -1.0f;  // -1이면 무한
    float m_currentTime = 0.0f;
    float dieDelay = 3.0f;
    float m_finishedTime = 0.0f;

    struct EmitterTiming {
        float startDelay = 0.0f;        // 이 emitter가 시작되는 시간
        bool hasStarted = false;         // 시작 여부
    };
    std::vector<EmitterTiming> m_emitterTimings;

public:
    EffectBase() = default;
    virtual ~EffectBase() = default;

    // 핵심 인터페이스
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

        // Emitter별 시작 시간 체크 및 업데이트
        for (size_t i = 0; i < m_particleSystems.size(); ++i) {
            auto& ps = m_particleSystems[i];
            if (!ps) continue;

            // 아직 시작 안한 emitter면 시간 체크
            if (!m_emitterTimings[i].hasStarted) {
                if (m_currentTime >= m_emitterTimings[i].startDelay) {
                    m_emitterTimings[i].hasStarted = true;
                    ps->Play();
                }
                else {
                    continue;  // 아직 시작 시간 안됨
                }
            }

            ps->SetEffectProgress(progressRatio);
            ps->Update(delta * m_timeScale);
        }

        // duration이 끝나면 Finished 상태로 변경 (Stop 대신)
        if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
            if (m_state == EffectState::Playing) {
                m_state = EffectState::Finished;

                // 새 파티클 스폰만 중단
                for (auto& ps : m_particleSystems) {
                    if (ps) {
                        ps->StopSpawning();
                    }
                }
            }
        }

        if (m_state == EffectState::Finished) {
            m_finishedTime += delta * m_timeScale;

            // 3초 후 또는 모든 파티클이 사라지면 Stopped로 전환
            if (m_finishedTime >= dieDelay) {
                m_state = EffectState::Stopped;
                m_finishedTime = 0.0f;

                // 시작된 것들만 정리
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

        // 모든 emitter의 시작 플래그 리셋
        for (auto& timing : m_emitterTimings) {
            timing.hasStarted = false;
        }

        // delay=0인 emitter들만 즉시 시작
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

        // 모든 emitter 정지 및 플래그 리셋
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
                ps->UpdateEffectBaseRotation(newRotation);
            }
        }
    }

    virtual void SetScale(const Mathf::Vector3& newScale) {
        m_scale = newScale;

        // 모든 ParticleSystem에 회전 적용
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->SetScale(m_scale);
            }
        }
    }

    // 풀링을 위한 재사용 리셋
    virtual void ResetForReuse() {
        m_state = EffectState::Stopped;
        m_currentTime = 0.0f;
        m_position = Mathf::Vector3(0, 0, 0);
        m_rotation = Mathf::Vector3(0, 0, 0);

        // Timing 정보 리셋
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


    // 재사용 준비 완료 여부 체크
    virtual bool IsReadyForReuse() const {
        // 정지 상태가 아니면 재사용 불가
        if (m_state != EffectState::Stopped) {
            return false;
        }

        // ParticleSystem들이 재사용 준비가 되었는지 확인
        for (const auto& ps : m_particleSystems) {
            if (ps) {
                // GPU 작업이 완료되지 않았으면 잠시 대기
                if (!ps->IsReadyForReuse()) {
                    // 한 번 더 기회를 주기 위해 GPU 완료 대기
                    ps->WaitForGPUCompletion();

                    // 다시 한 번 확인
                    if (!ps->IsReadyForReuse()) {
                        return false;
                    }
                }
            }
        }
        return true;
    }


    // GPU 작업 완료 대기
    void WaitForGPUCompletion() {
        //for (auto& ps : m_particleSystems) {
        //    if (ps) {
        //        ps->WaitForGPUCompletion();
        //    }
        //}

        // 전체 GPU 파이프라인 플러시
        //if (DirectX11::DeviceStates->g_pDeviceContext) {
        //    DirectX11::DeviceStates->g_pDeviceContext->Flush();
        //
        //    // 추가 동기화 (필요한 경우)
        //    //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        //}
    }

    const Mathf::Vector3& GetRotation() const { return m_rotation; }

    // ParticleSystem 관리
    void AddParticleSystem(std::shared_ptr<ParticleSystem> ps) {
        if (ps) {
            m_particleSystems.push_back(ps);

            // Timing 정보도 함께 추가
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
        m_emitterTimings.clear();  // Timing도 함께 클리어
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