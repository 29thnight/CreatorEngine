#pragma once
#include "nlohmann/json.hpp"

class ISerializable
{
public:
    virtual ~ISerializable() = default;

    // 각 모듈이 자신만의 데이터를 JSON으로 저장
    virtual nlohmann::json SerializeData() const = 0;

    // JSON에서 자신의 데이터를 복원
    virtual void DeserializeData(const nlohmann::json& json) = 0;

    // 모듈 타입을 문자열로 반환 (클래스명과 동일하게)
    virtual std::string GetModuleType() const = 0;
};