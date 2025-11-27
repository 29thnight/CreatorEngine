# Prefab

**Header:** `ScriptBinder/Prefab.h`

**Inheritance:** `: public Object`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `Prefab() = default;`
- `Prefab(std::string_view name, const GameObject* source);`
- `~Prefab() override = default;`
- `static Prefab* CreateFromGameObject(const GameObject* source, std::string_view name = "");`
- `GameObject* Instantiate(std::string_view newName = "") const;`
- `GameObject* Instantiate(Scene* targetScene, std::string_view newName = "") const;`

## Public Properties
- (none)
