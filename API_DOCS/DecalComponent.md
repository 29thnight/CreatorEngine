# DecalComponent

**Header:** `ScriptBinder/DecalComponent.h`

**Inheritance:** `: public Component, public RegistableEvent<DecalComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Awake() override;`
- `void Update(float deltaSeconds) override;`
- `void OnDestroy() override;`
- `void SetDecalTexture(const std::string_view& fileName);`
- `void SetDecalTexture(const FileGuid& fileGuid);`
- `void SetNormalTexture(const std::string_view& fileName);`
- `void SetNormalTexture(const FileGuid& fileGuid);`
- `void SetORMTexture(const std::string_view& fileName);`
- `void SetORMTexture(const FileGuid& fileGuid);`

## Public Properties
- `uint32 sliceX = 1;`
- `uint32 sliceY = 1;`
- `int sliceNumber = 0;`
- `float slicePerSeconds = 1.f;`
- `float timer = 0.f;`
- `bool useAnimation = false;`
- `bool isLoop = true;`
