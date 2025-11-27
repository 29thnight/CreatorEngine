# Animator

**Header:** `ScriptBinder/Animator.h`

**Inheritance:** `: public Component, public RegistableEvent<Animator>, public std::enable_shared_from_this<Animator>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Awake` | awake 동작을 수행합니다. |
| `Update` | update을(를) 갱신합니다. |
| `OnDestroy` | on destroy 동작을 수행합니다. |
| `SetAnimation` | animation을(를) 설정합니다. |
| `UpdateAnimation` | animation을(를) 갱신합니다. |
| `CreateController` | controller을(를) 생성합니다. |
| `CreateController_UI` | controller ui을(를) 생성합니다. |
| `CreateController_UINoAni` | controller uino ani을(를) 생성합니다. |
| `DeleteController` | delete controller 동작을 수행합니다. |
| `GetController` | controller을(를) 가져옵니다. |
| `SerializeControllers` | serialize controllers 동작을 수행합니다. |
| `DeserializeControllers` | deserialize controllers 동작을 수행합니다. |
| `SetUseLayer` | use layer을(를) 설정합니다. |
| `FindBoneRecursive` | bone recursive을(를) 탐색합니다. |
| `MakeSocket` | make socket 동작을 수행합니다. |
| `FindSocket` | socket을(를) 탐색합니다. |
| `ClearControllersAndParams` | controllers and params을(를) 비웁니다. |
| `AddParameter` | parameter을(를) 추가합니다. |
| `DeleteParameter` | delete parameter 동작을 수행합니다. |
| `AddDefaultParameter` | default parameter을(를) 추가합니다. |
| `SetParameter` | parameter을(를) 설정합니다. |
| `FindParameter` | parameter을(를) 탐색합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `blendT` | blend t 상태를 보관합니다. |
| `nextAnimIndex` | next anim index 상태를 보관합니다. |
| `blendtransform` | blendtransform 상태를 보관합니다. |
| `socketvec` | socketvec 상태를 보관합니다. |
| `Parameters` | parameters 상태를 보관합니다. |
| `m_paramMutex` | m param mutex 상태를 보관합니다. |
| `m_isBlend` | m is blend 상태를 보관합니다. |
| `m_stopTimer` | m stop timer 상태를 보관합니다. |
| `m_stopDuration` | m stop duration 상태를 보관합니다. |
