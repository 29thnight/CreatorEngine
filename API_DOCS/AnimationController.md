# AnimationController

**Header:** `ScriptBinder/AnimationController.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `AnimationController() = default;`
- `~AnimationController();`
- `bool BlendingAnimation(float tick);`
- `void SetCurState(std::string stateName);`
- `void SetNextState(std::string stateName);`
- `std::shared_ptr<AniTransition> CheckTransition();`
- `void UpdateState();`
- `void Update(float tick);`
- `int GetAnimatonIndexformState(std::string stateName);`
- `std::shared_ptr<AnimationState> GetAniState();`
- `AnimationState* CreateState(const std::string& stateName, int animationIndex,bool isAny = false);`
- `std::shared_ptr<AnimationState> CreateState_UI();`
- `void DeleteState(std::string stateName);`
- `void DeleteTransiton(const std::string& fromStateName, const std::string& toStateName);`
- `AnimationState* FindState(std::string stateName);`
- `AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName);`
- `void CreateMask();`
- `void ReCreateMask(AvatarMask* mask);`
- `void DeleteAvatarMask();`
- `nlohmann::json Serialize();`
- `void Deserialize();`
- `void SetUseLayer(bool _useLayer);`

## Public Properties
- `std::string name = "None";`
- `AnimationState* m_curState = nullptr;`
- `AnimationState* m_nextState = nullptr;`
- `std::vector<std::shared_ptr<AnimationState>> StateVec;`
- `std::unordered_map<std::string, std::weak_ptr<AnimationState>> m_nameToState;`
- `std::set<std::string> StateNameSet;`
- `NodeEditor* m_nodeEditor;`
- `std::shared_ptr<AnimationState> m_anyState;`
- `float m_timeElapsed;`
- `float m_nextTimeElapsed;`
- `float curAnimationProgress = 0.f;`
- `float preCurAnimationProgress = 0.f;`
- `float nextAnimationProgress = 0.f;`
- `float preNextAnimationProgress = 0.f;`
- `bool needBlend = false;`
- `bool m_isBlend = false;`
- `bool useController = true;`
- `bool m_useLayer = true;`
- `bool useMask = false;`
- `bool endAnimation = false;`
