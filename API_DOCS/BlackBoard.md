# BlackBoard

**Header:** `ScriptBinder/Blackboard.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void SetValueAsBool(const std::string& key, bool value);`
- `void SetValueAsInt(const std::string& key, int value);`
- `void SetValueAsFloat(const std::string& key, float value);`
- `void SetValueAsString(const std::string& key, const std::string& value);`
- `void SetValueAsVector2(const std::string& key, const Mathf::Vector2& value);`
- `void SetValueAsVector3(const std::string& key, const Mathf::Vector3& value);`
- `void SetValueAsVector4(const std::string& key, const Mathf::Vector4& value);`
- `void SetValueAsGameObject(const std::string& key, const std::string& objectName);`
- `void SetValueAsTransform(const std::string& key, const std::string& transformPath);`
- `bool GetValueAsBool(const std::string& key) const;`
- `int GetValueAsInt(const std::string& key) const;`
- `float GetValueAsFloat(const std::string& key) const;`
- `const std::string& GetValueAsString(const std::string& key) const;`
- `const Mathf::Vector2& GetValueAsVector2(const std::string& key) const;`
- `const Mathf::Vector3& GetValueAsVector3(const std::string& key) const;`
- `const Mathf::Vector4& GetValueAsVector4(const std::string& key) const;`
- `GameObject* GetValueAsGameObject(const std::string& key) const;`
- `const Transform& GetValueAsTransform(const std::string& key) const;`
- `void AddKey(const std::string& key, const BlackBoardType& type);`
- `bool HasKey(const std::string& key) const;`
- `BlackBoardType GetType(const std::string& key) const;`
- `void RemoveKey(const std::string& key);`
- `void RenameKey(const std::string& curKey, const std::string& newKey);`
- `void Clear();`
- `void Serialize(std::string_view name);`
- `void Deserialize(std::string_view name);`

## Public Properties
- (none)
