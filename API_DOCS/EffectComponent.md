# EffectComponent

**Header:** `ScriptBinder/EffectComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<EffectComponent>, public System::IInitializable, public std::enable_shared_from_this<EffectComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Initialize` | initialize 동작을 수행합니다. |
| `Update` | update을(를) 갱신합니다. |
| `OnDestroy` | on destroy 동작을 수행합니다. |
| `Apply` | apply을(를) 적용합니다. |
| `PlayPreview` | play preview 동작을 수행합니다. |
| `StopEffect` | stop effect 동작을 수행합니다. |
| `PauseEffect` | pause effect 동작을 수행합니다. |
| `ResumeEffect` | resume effect 동작을 수행합니다. |
| `ChangeEffect` | change effect 동작을 수행합니다. |
| `PlayEffectByName` | play effect by name 동작을 수행합니다. |
| `SetLoop` | loop을(를) 설정합니다. |
| `SetDuration` | duration을(를) 설정합니다. |
| `SetTimeScale` | time scale을(를) 설정합니다. |
| `ForceFinishEffect` | force finish effect 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `m_effectTemplateName` | m effect template name 상태를 보관합니다. |
| `m_timeScale` | m time scale 상태를 보관합니다. |
| `m_duration` | m duration 상태를 보관합니다. |
| `m_isPlaying` | m is playing 상태를 보관합니다. |
| `m_isPaused` | m is paused 상태를 보관합니다. |
| `m_loop` | m loop 상태를 보관합니다. |
| `m_useAbsolutePosition` | m use absolute position 상태를 보관합니다. |
| `m_currentTime` | m current time 상태를 보관합니다. |
| `m_effectInstanceName` | m effect instance name 상태를 보관합니다. |
