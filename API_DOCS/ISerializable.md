# ISerializable

**Header:** `ScriptBinder/ISerializable.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `virtual ~ISerializable() = default;`
- `virtual nlohmann::json SerializeData() const = 0;`
- `virtual void DeserializeData(const nlohmann::json& json) = 0;`
- `virtual std::string GetModuleType() const = 0;`

## Public Properties
- (none)
