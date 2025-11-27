# RectTransformComponent

**Header:** `ScriptBinder/RectTransformComponent.h`

**Inheritance:** `: public Component`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `RectTransformComponent();`
- `virtual ~RectTransformComponent() = default;`
- `void UpdateLayout(const Mathf::Rect& parentRect);`
- `void SetAnchorMin(const Mathf::Vector2& anchorMin);`
- `void SetAnchorMax(const Mathf::Vector2& anchorMax);`
- `Mathf::Vector2 GetAnchoredPosition() const;`
- `void SetAnchoredPosition(const Mathf::Vector2& position);`
- `void SetSizeDelta(const Mathf::Vector2& size);`
- `void SetPivot(const Mathf::Vector2& pivot);`
- `void SetAnchorPreset(AnchorPreset preset);`
- `void SetParentKeepWorldPosition(GameObject* newParent);`

## Public Properties
- `const Mathf::Rect& newParentRect);`
