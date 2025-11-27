# AnimationState

**Header:** `ScriptBinder/AnimationState.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `AnimationState();`
- `~AnimationState();`
- `AnimationState(AnimationController* Owner, std::string name);`
- `std::vector<AniTransition*> FindTransitions(const std::string& toStateName);`
- `void SetBehaviour(std::string name, bool isReload = false);`
- `void UpdateAnimationSpeed();`
- `nlohmann::json Serialize();`
- `AnimationState Deserialize();`

## Public Properties
- `std::vector<std::shared_ptr<AniTransition>> Transitions;`
- `int index =0;`
- `int AnimationIndex = 0;`
- `float animationSpeed = 1;`
- `float multiplerAnimationSpeed = 1;`
- `std::string animationSpeedParameterName = "None";`
- `float m_animationTimeElapsed = 0;`
- `bool m_isAny = false;`
- `bool useMultipler = false;`
