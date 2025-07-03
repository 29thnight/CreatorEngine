#pragma once
#include "nlohmann/json.hpp"

class ISerializable
{
public:
    virtual ~ISerializable() = default;

    // �� ����� �ڽŸ��� �����͸� JSON���� ����
    virtual nlohmann::json SerializeData() const = 0;

    // JSON���� �ڽ��� �����͸� ����
    virtual void DeserializeData(const nlohmann::json& json) = 0;

    // ��� Ÿ���� ���ڿ��� ��ȯ (Ŭ������� �����ϰ�)
    virtual std::string GetModuleType() const = 0;
};