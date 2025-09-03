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
    Mathf::Vector3 m_scale = Mathf::Vector3(0, 0, 0);
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
        if (m_state != EffectState::Playing) return;

        // 시간 업데이트
        m_currentTime += delta * m_timeScale;

        // 진행률 계산
        float progressRatio = 0.0f;
        bool isInfinite = (m_duration < 0);

        if (isInfinite) {
            // 무한 재생 모드 - 고정된 주기로 0~1 반복 (예: 1초 주기)
            progressRatio = std::fmod(m_currentTime, 1.0f);
        }
        else if (m_duration > 0) {
            progressRatio = std::clamp(m_currentTime / m_duration, 0.0f, 1.0f);
        }

        // ParticleSystem 업데이트
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->SetEffectProgress(progressRatio);
                ps->Update(delta * m_timeScale);
            }
        }

        // duration이 끝나면 바로 Stop
        if (!isInfinite && m_duration > 0 && m_currentTime >= m_duration) {
            Stop();
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
        // 상태 강제 정지
        m_state = EffectState::Stopped;
        m_currentTime = 0.0f;

        // 위치/회전 초기화
        m_position = Mathf::Vector3(0, 0, 0);
        m_rotation = Mathf::Vector3(0, 0, 0);

        // GPU 작업 완료 대기
        WaitForGPUCompletion();

        // ParticleSystem들 리셋
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
        for (auto& ps : m_particleSystems) {
            if (ps) {
                ps->WaitForGPUCompletion();
            }
        }

        // 전체 GPU 파이프라인 플러시
        if (DirectX11::DeviceStates->g_pDeviceContext) {
            DirectX11::DeviceStates->g_pDeviceContext->Flush();

            // 추가 동기화 (필요한 경우)
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
};