# SoundComponent

**Header:** `ScriptBinder/SoundComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<SoundComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Start() override;`
- `void Update(float tick) override;`
- `void LateUpdate(float tick) override;`
- `void OnDestroy() override;`
- `void Play();`
- `void Stop();`
- `void Pause(bool pause);`
- `bool IsPlaying();`
- `void PlayOneShot();`
- `void EditorSet();`

## Public Properties
- `std::string clipKey;`
- `ChannelType bus = ChannelType::SFX;`
- `float volume = 1.f;`
- `float pitch = 1.f;`
- `int priority = 128;`
- `float spatialBlend = 1.0f;`
- `float minDistance = 1.0f;`
- `float maxDistance = 50.0f;`
- `float  reverbLevel = 0.0f;`
- `int    reverbIndex = 0;`
- `Rolloff rolloff = Rolloff::Inverse;`
- `std::vector<CurvePoint> localRolloffCurve;`
- `bool loop = false;`
- `bool playOnStart = false;`
- `bool spatial = false;`
- `bool useReverbSend = false;`
