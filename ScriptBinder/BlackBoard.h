#pragma once
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <string>
#include <any>

class BlackBoard
{
public:
	using BBValue = std::any;

	template<typename T>
	void Set(const std::string& key, const T& value)
	{
		m_blackBoard[key] = value;
	}

	template<typename T>
	T& Get(const std::string& key) const
	{
		auto it = m_blackBoard.find(key);
		if (it != m_blackBoard.end())
		{
			return std::any_cast<T&>(it->second);
		}
		throw std::runtime_error("Key not found in BlackBoard");
	}

	template<typename T>
	bool HasType(const std::string& key) const
	{
		auto it = m_blackBoard.find(key);
		if (it != m_blackBoard.end())
		{
			return it->second.type() == typeid(T);
		}
		return false;
	}

	bool Has(const std::string& key) const
	{
		return m_blackBoard.find(key) != m_blackBoard.end();
	}

	

private :
	std::unordered_map<std::string, BBValue> m_blackBoard;
};


using ConditionFunc = std::function<bool(const BlackBoard&)>;