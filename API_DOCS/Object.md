# Object

**Header:** `ScriptBinder/Object.h`

**Inheritance:** `: public IObject, public Managed::HeapObject`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `Object() = default;`
- `virtual ~Object() = default;`
- `virtual void Destroy();`
- `static void Destroy(Object* objPtr);`
- `static void SetDontDestroyOnLoad(Object* objPtr);`
- `static Object* Instantiate(const Object* original, std::string_view newName);`

## Public Properties
- (none)
