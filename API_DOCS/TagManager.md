# TagManager

**Header:** `ScriptBinder/TagManager.h`

**Inheritance:** `: public DLLCore::Singleton<TagManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Initialize();`
- `void Finalize();`
- `void Load();`
- `void Save();`
- `void AddTag(std::string_view tag);`
- `void RemoveTag(std::string_view tag);`
- `bool HasTag(std::string_view tag) const;`
- `void AddLayer(std::string_view layer);`
- `void RemoveLayer(std::string_view layer);`
- `bool HasLayer(std::string_view layer) const;`
- `void AddTagToObject(std::string_view tag, GameObject* object);`
- `void RemoveTagFromObject(std::string_view tag, GameObject* object);`
- `void AddObjectToLayer(std::string_view layer, GameObject* object);`
- `void RemoveObjectFromLayer(std::string_view layer, GameObject* object);`

## Public Properties
- (none)
