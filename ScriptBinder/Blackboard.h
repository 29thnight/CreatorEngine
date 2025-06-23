#pragma once
#include "../Utility_Framework/LogSystem.h"
#include <string>
#include <unordered_map>
#include <any>

//using BBValue = std::variant<bool, int, float, std::string>;
using BBValue = std::any;

class Blackboard
{
public:
	void Set(const std::string& key, const BBValue& value);
	//BBValue Get(const std::string& key) const;
	bool Has(const std::string& key) const;

	template<typename T>
	T& Get(const std::string& name) const
	{
		BBValue any = m_data[name];
		try
		{
			T& value = std::any_cast<T&>(any);
			return value;
		}
		catch (const std::exception& e)
		{
			//fuxk
			throw std::runtime_error("Blackboard: Key '" + name + "' not found or type mismatch. " + e.what());
		}
		
	}

private:
	// 데이터 저장을 위한 맵
	std::unordered_map<std::string, BBValue> m_data;
};