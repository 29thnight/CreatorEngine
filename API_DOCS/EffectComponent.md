# EffectComponent

**Header:** `ScriptBinder/EffectComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<EffectComponent>, public System::IInitializable, public std::enable_shared_from_this<EffectComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Initialize() override;`
- `void Update(float tick) override;`
- `void OnDestroy() override;`
- `void Apply();`
- `void PlayPreview();`
- `void StopEffect();`
- `void PauseEffect();`
- `void ResumeEffect();`
- `void ChangeEffect(const std::string& newEffectName);`
- `void PlayEffectByName(const std::string& effectName);`
- `void SetLoop(bool loop);`
- `void SetDuration(float duration);`
- `void SetTimeScale(float timeScale);`
- `void ForceFinishEffect();`

## Public Properties
- `std::string m_effectTemplateName = "Null";`
- `float m_timeScale = 1.0f;`
- `float m_duration = -1.0f;`
- `bool m_isPlaying = false;`
- `bool m_isPaused = false;`
- `bool m_loop = true;`
- `bool m_useAbsolutePosition = false;`
- `float m_currentTime = 0;`
- `std::string m_effectInstanceName;`
