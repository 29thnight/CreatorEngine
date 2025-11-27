# UIComponent

**Header:** `ScriptBinder/UIComponent.h`

**Inheritance:** `: public Component`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `UIComponent();`
- `virtual ~UIComponent() = default;`
- `void SetCanvas(Canvas* canvas);`
- `void SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI);`
- `void DeserializeNavi();`
- `GameObject* GetNextNavi(Direction dir);`
- `bool IsNavigationThis();`
- `void DeserializeShader();`
- `void SetCustomPixelShader(std::string_view shaderPath);`
- `std::optional<float> GetFloat(std::string_view name) const;`
- `void SetFloat(std::string_view name, float value);`
- `std::optional<float2> GetFloat2(std::string_view name) const;`
- `void SetFloat2(std::string_view name, const float2& value);`
- `std::optional<float3> GetFloat3(std::string_view name) const;`
- `void SetFloat3(std::string_view name, const float3& value);`
- `std::optional<float4> GetFloat4(std::string_view name) const;`
- `void SetFloat4(std::string_view name, const float4& value);`
- `std::optional<int> GetInt(std::string_view name) const;`
- `void SetInt(std::string_view name, int value);`
- `std::optional<int2> GetInt2(std::string_view name) const;`
- `void SetInt2(std::string_view name, const int2& value);`
- `std::optional<int3> GetInt3(std::string_view name) const;`
- `void SetInt3(std::string_view name, const int3& value);`
- `std::optional<int4> GetInt4(std::string_view name) const;`
- `void SetInt4(std::string_view name, const int4& value);`

## Public Properties
- `UItype type = UItype::None;`
- `bool isDeserialized = false;`
- `bool isNavLocked = false;`
