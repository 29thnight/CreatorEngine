#include "Blackboard.h"

void Blackboard::Set(const std::string& key, const BBValue& value) {
	m_data[key] = value;
}

//BBValue Blackboard::Get(const std::string& key) const {
//	auto it = m_data.find(key);
//	if (it == m_data.end()) {
//		Debug->LogDebug("Blackboard : key " + key+"not found");
//		return BBValue(); // 기본값 반환 (빈 variant)
//	}
//	return it->second;
//}

bool Blackboard::Has(const std::string& key) const {
	return m_data.find(key) != m_data.end();
}