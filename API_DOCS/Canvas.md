# Canvas

**Header:** `ScriptBinder/Canvas.h`

**Inheritance:** `: public Component, public RegistableEvent<Canvas>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `Canvas();`
- `~Canvas() = default;`
- `void OnDestroy() override;`
- `void AddUIObject(std::shared_ptr<GameObject> obj);`
- `virtual void Update(float tick) override;`
- `std::weak_ptr<GameObject> GetFrontUIObject();`

## Public Properties
- `int PreCanvasOrder = 0;`
- `int CanvasOrder = 0;`
- `std::vector<std::weak_ptr<GameObject>> UIObjs;`
- `std::string CanvasName = "Canvas";`
