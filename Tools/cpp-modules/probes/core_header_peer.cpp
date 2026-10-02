// 헤더로만 Utility_Framework 공용층(Uuid.h · ClassProperty.h)을 보는 번역 단위.
#include <string>
#include <typeinfo>

#include "ClassProperty.h"
#include "Uuid.h"

#include "core_probe_singleton.h"
#include "core_peer.h"

namespace probe::core_peer
{
	Uuid::Uuid16 v5_from_header(const char* name)
	{
		const Uuid::Uuid16 dns = Uuid::Parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
		return Uuid::FromName(dns, name);
	}

	const std::type_info& uuid16_type()
	{
		return typeid(Uuid::Uuid16);
	}

	CppModuleProbeSingleton* singleton_from_header()
	{
		return CppModuleProbeSingleton::GetInstance();
	}

	int singleton_value_from_header()
	{
		CppModuleProbeSingleton* instance = CppModuleProbeSingleton::GetIfAlive();
		return instance ? instance->value : -1;
	}

	void destroy_singleton_from_header()
	{
		CppModuleProbeSingleton::Destroy();
	}
}
