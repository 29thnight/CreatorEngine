# Animator

**Header:** `ScriptBinder/Animator.h`

**Inheritance:** `: public Component, public RegistableEvent<Animator>, public std::enable_shared_from_this<Animator>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Awake() override;`
- `void Update(float tick) override;`
- `void OnDestroy() override;`
- `void SetAnimation(int index);`
- `void UpdateAnimation();`
- `void CreateController(std::string name);`
- `std::shared_ptr<AnimationController> CreateController_UI();`
- `std::shared_ptr<AnimationController> CreateController_UINoAni();`
- `void DeleteController(int index);`
- `void DeleteController(std::string controllerName);`
- `AnimationController* GetController(std::string name);`
- `void SerializeControllers(std::string _jsonName);`
- `void DeserializeControllers(std::string _filename);`
- `void SetUseLayer(int layerindex,bool _useLayer);`
- `GameObject* FindBoneRecursive(GameObject* parent, const std::string& boneName);`
- `Socket* MakeSocket(std::string_view socketName,std::string_view boneName, GameObject* object);`
- `Socket* FindSocket(std::string_view socketName);`
- `void ClearControllersAndParams();`
- `void AddParameter(const std::string valuename, T value, ValueType vType);`
- `void DeleteParameter(int index);`
- `ConditionParameter* AddDefaultParameter(ValueType vType);`
- `void SetParameter(const std::string valuename, T Value);`
- `ConditionParameter* FindParameter(std::string valueName);`

## Public Properties
- `float blendT = 0;`
- `int nextAnimIndex = -1;`
- `XMMATRIX blendtransform;`
- `std::vector<Socket*> socketvec;`
- `std::vector<ConditionParameter*> Parameters;`
- `std::mutex m_paramMutex;`
- `bool m_isBlend = false;`
- `float m_stopTimer = 0.f;`
- `float m_stopDuration = 0.f;`
