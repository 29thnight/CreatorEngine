# AnimationController

**Header:** `ScriptBinder/AnimationController.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `AnimationController` | animation controller 동작을 수행합니다. |
| `~AnimationController` | animation controller 동작을 수행합니다. |
| `BlendingAnimation` | blending animation 동작을 수행합니다. |
| `SetCurState` | cur state을(를) 설정합니다. |
| `SetNextState` | next state을(를) 설정합니다. |
| `CheckTransition` | check transition 동작을 수행합니다. |
| `UpdateState` | state을(를) 갱신합니다. |
| `Update` | update을(를) 갱신합니다. |
| `GetAnimatonIndexformState` | animaton indexform state을(를) 가져옵니다. |
| `GetAniState` | ani state을(를) 가져옵니다. |
| `CreateState` | state을(를) 생성합니다. |
| `CreateState_UI` | state ui을(를) 생성합니다. |
| `DeleteState` | delete state 동작을 수행합니다. |
| `DeleteTransiton` | delete transiton 동작을 수행합니다. |
| `FindState` | state을(를) 탐색합니다. |
| `CreateTransition` | transition을(를) 생성합니다. |
| `CreateMask` | mask을(를) 생성합니다. |
| `ReCreateMask` | re create mask 동작을 수행합니다. |
| `DeleteAvatarMask` | delete avatar mask 동작을 수행합니다. |
| `Serialize` | serialize 동작을 수행합니다. |
| `Deserialize` | deserialize 동작을 수행합니다. |
| `SetUseLayer` | use layer을(를) 설정합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `name` | name 상태를 보관합니다. |
| `m_curState` | m cur state 상태를 보관합니다. |
| `m_nextState` | m next state 상태를 보관합니다. |
| `StateVec` | state vec 상태를 보관합니다. |
| `m_nameToState` | m name to state 상태를 보관합니다. |
| `StateNameSet` | state name set 상태를 보관합니다. |
| `m_nodeEditor` | m node editor 상태를 보관합니다. |
| `m_anyState` | m any state 상태를 보관합니다. |
| `m_timeElapsed` | m time elapsed 상태를 보관합니다. |
| `m_nextTimeElapsed` | m next time elapsed 상태를 보관합니다. |
| `curAnimationProgress` | cur animation progress 상태를 보관합니다. |
| `preCurAnimationProgress` | pre cur animation progress 상태를 보관합니다. |
| `nextAnimationProgress` | next animation progress 상태를 보관합니다. |
| `preNextAnimationProgress` | pre next animation progress 상태를 보관합니다. |
| `needBlend` | need blend 상태를 보관합니다. |
| `m_isBlend` | m is blend 상태를 보관합니다. |
| `useController` | use controller 상태를 보관합니다. |
| `m_useLayer` | m use layer 상태를 보관합니다. |
| `useMask` | use mask 상태를 보관합니다. |
| `endAnimation` | end animation 상태를 보관합니다. |
