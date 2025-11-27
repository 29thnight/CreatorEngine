# InputActionManager

**Header:** `ScriptBinder/InputActionManager.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `~InputActionManager() = default;`
- `void Update(float tick);`
- `void AddActionMap();`
- `ActionMap* AddActionMap(std::string name);`
- `void DeleteActionMap(std::string name);`
- `ActionMap* FindActionMap(std::string name);`
- `void SaveManager();`
- `void LoadManager();`
- `nlohmann::json SerializeMap(ActionMap* _actionMap);`
- `ActionMap* DeSerializeMap(std::string _filepath);`

## Public Properties
- `std::vector<ActionMap*> m_actionMaps;`
