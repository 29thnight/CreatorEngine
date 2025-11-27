# UIManager

**Header:** `ScriptBinder/UIManager.h`

**Inheritance:** `: public DLLCore::Singleton<UIManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `std::shared_ptr<GameObject> MakeCanvas(std::string_view name = "Canvas");`
- `void AddCanvas(std::shared_ptr<GameObject> canvas);`
- `void DeleteCanvas(const std::shared_ptr<GameObject>& canvas);`
- `void CheckInput();`
- `GameObject* FindCanvasName(const std::shared_ptr<GameObject>& obj, std::string_view name);`
- `GameObject* FindCanvasIndex(const std::shared_ptr<GameObject>& obj, int index);`
- `GameObject* FindCanvasName(std::string_view name);`
- `GameObject* FindCanvasIndex(int index);`
- `void Update();`
- `void SortCanvas();`
- `void RegisterImageComponent(ImageComponent* image);`
- `void RegisterTextComponent(TextComponent* text);`
- `void RegisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);`
- `void UnregisterImageComponent(ImageComponent* image);`
- `void UnregisterTextComponent(TextComponent* text);`
- `void UnregisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);`

## Public Properties
- `friend class DLLCore::Singleton<UIManager>;`
- `Core::Delegate<void, Mathf::Vector2> m_clickEvent;`
- `std::vector<ImageComponent*>			Images;`
- `std::vector<TextComponent*>			Texts;`
- `std::vector<SpriteSheetComponent*>    SpriteSheets;`
- `std::weak_ptr<GameObject> CurCanvas;`
- `std::weak_ptr<GameObject> SelectUI;`
- `bool needSort = false;`
