# AniTransition

**Header:** `ScriptBinder/AniTransition.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `AniTransition() = default;`
- `AniTransition(AnimationState* _curState, AnimationState* _nextState);`
- `~AniTransition();`
- `void DeleteCondition(int _index);`
- `void SetCurState(std::string _curStateName);`
- `void SetCurState(AnimationState* _curState);`
- `void SetNextState(std::string _nextStateName);`
- `void SetNextState(AnimationState* _nextStat);`
- `std::string GetCurState();`
- `std::string GetNextState();`
- `bool CheckTransiton(bool isBlend = false);`
- `nlohmann::json Serialize();`
- `AniTransition Deserialize();`
- `std::vector<TransCondition> GetConditions();`

## Public Properties
- `std::string m_name = "NoName";`
- `AnimationState* curState = nullptr;`
- `AnimationState* nextState = nullptr;`
- `float exitTime = 0.1f;`
- `float blendTime = 0.2f;`
- `bool hasExitTime = false;`
