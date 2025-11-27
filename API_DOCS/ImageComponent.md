# ImageComponent

**Header:** `ScriptBinder/ImageComponent.h`

**Inheritance:** `: public UIComponent, public RegistableEvent<ImageComponent>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `ImageComponent();`
- `~ImageComponent() = default;`
- `void Load(const std::shared_ptr<Texture>& ptr);`
- `void DeserializeTexture(const std::shared_ptr<Texture>& ptr);`
- `virtual void Awake() override;`
- `virtual void Update(float tick) override;`
- `virtual void OnDestroy() override;`
- `void UpdateTexture();`
- `void SetTexture(int index);`
- `void ResetSize();`
- `bool isThisTextureExist(std::string_view path) const;`

## Public Properties
- (none)
