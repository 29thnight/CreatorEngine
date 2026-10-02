#pragma once
// 헤더로만 Utility_Framework 공용층을 본 번역 단위(core_header_peer.cpp)가 내놓는 창.
//
// ★ Uuid · Singleton 이 이미 보이는 자리에서, core_probe_singleton.h 뒤에 연다.
//   시그니처의 Uuid::Uuid16 이 두 쪽에서 같은 엔터티여야 링크가 맞는다.
#include <string>
#include <typeinfo>

namespace probe::core_peer
{
	// Uuid::FromName(DNS 이름공간, name) 를 헤더 쪽에서 계산한 값.
	Uuid::Uuid16 v5_from_header(const char* name);

	const std::type_info& uuid16_type();

	CppModuleProbeSingleton* singleton_from_header();
	int  singleton_value_from_header();
	void destroy_singleton_from_header();
}
