# TransCondition

**Header:** `ScriptBinder/TransCondition.h`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `TransCondition() = default;`
- `bool CheckTrans();`
- `void SetValue(std::string valueName);`
- `void SetCondition(std::string _parameterName);`
- `nlohmann::json Serialize();`
- `TransCondition Deserialize();`

## Public Properties
- `std::string valueName = "None";`
- `ConditionParameter* valueParameter;`
- `ConditionParameter CompareParameter;`
- `ConditionType cType = ConditionType::Equal;`
