# MeshRenderer

**Header:** `ScriptBinder/MeshRenderer.h`

**Inheritance:** `: public Component, public RegistableEvent<MeshRenderer>, public std::enable_shared_from_this<MeshRenderer>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `MeshRenderer();`
- `virtual ~MeshRenderer() override;`
- `virtual void Awake() override;`
- `virtual void OnDestroy() override;`
- `BoundingBox GetBoundingBox() const;`

## Public Properties
- `LightMapping m_LightMapping;`
- `bool m_shadowRecive = true;`
- `bool m_shadowCast = true;`
