#pragma once
// 엔진의 런타임 타입 창구 (reflgen 도입 P5) — 이름·typeID 로 reflgen 서술자를 찾는다. Meta::Type·Meta::Registry 를
// 대신한다.
//
// 등록소는 프로세스에 하나다 — 정의는 ReflgenRuntime.cpp(헤더 인라인 싱글턴 금지 규약,
// verify-header-inline-singleton.ps1). reflgen::default_registry() 는 쓰지 않는다: 헤더 인라인이라 엔진을 DLL 로
// 떼면 모듈마다 갈린다. 채우는 것은 생성된 등록 함수(reflgen::generated::register_<모듈>)다 — 런타임 모듈은
// RegisterReflectManual(), Editor 모듈은 에디터 부트스트랩이 부른다.
//
// ★ 서술자는 등록 함수의 번역 단위에서 만들어진다 — 그곳이 엔진 serializer 특수화를 본다(ReflgenRegistrationHeaders).
//   소비자는 여기서 찾기만 한다. reflgen::type_descriptor_of<T>() 를 직접 부르지 않는다: 그 번역 단위에서 서술자를
//   다시 만들고(컴파일 비용), 엔진 특수화를 못 보는 번역 단위라면 다른 서술자가 된다(ODR 위반).
//
// Meta::Type 과의 대응:
//   name · typeID              → name() · TypeIDOf() (같은 값 — 둘 다 한정 이름의 FNV-1a 64, RegisterReflectManual.h 가
//                                 반영 타입마다 컴파일 때 대조한다)
//   properties · methods       → LocalFields · LocalMethods (그 타입이 선언한 것만 — 부모 것은 Parent 로 올라간다)
//                                 fields() · methods() 는 부모 것까지 부모 우선으로 전부다
//   parent                     → Parent
//   create · createUnique      → Create<Base>
//   Property::enumType         → field_info::enumeration()
//   Property::hasRange 등      → field_info::attributes() (creator:: 속성)
#include "TypeTrait.h"
#include <reflgen/runtime/registry.h>
#include "../SceneRuntime/SceneGC.h"
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Meta
{
	reflgen::registry& Types();

	inline reflgen::type_id ToTypeID(HashedGuid typeID)
	{
		return reflgen::type_id{ static_cast<std::uint64_t>(typeID.m_ID_Data) };
	}

	inline HashedGuid TypeIDOf(const reflgen::type_descriptor& type)
	{
		return HashedGuid{ static_cast<size_t>(type.id().value()) };
	}

	inline const reflgen::type_descriptor* Find(std::string_view name)
	{
		return Types().find(name);
	}

	inline const reflgen::type_descriptor* Find(HashedGuid typeID)
	{
		return Types().find(ToTypeID(typeID));
	}

	// 등록돼 있어야 하는 타입 — 없으면 등록 누락이다(조용히 넘기지 않는다).
	inline const reflgen::type_descriptor& Require(HashedGuid typeID, std::string_view typeName)
	{
		if (const reflgen::type_descriptor* type = Find(typeID))
		{
			return *type;
		}
		throw std::logic_error("reflgen type not registered: " + std::string(typeName)
			+ " (RegisterReflectManual / register_Editor 를 확인하라)");
	}

	template<class T>
	const reflgen::type_descriptor& TypeOf()
	{
		return Require(TypeTrait::GUIDCreator::GetTypeID<T>(), TypeTrait::type_name<T>());
	}

	// 가장 가까운 반영된 부모(첫 base) — Meta::Type::parent.
	inline const reflgen::type_descriptor* Parent(const reflgen::type_descriptor& type)
	{
		return type.bases().empty() ? nullptr : type.bases().front().descriptor();
	}

	// 이 타입이 선언한 필드 — fields() 는 부모들의 fields() 뒤에 자기 것을 잇는다.
	inline std::span<const reflgen::field_info> LocalFields(const reflgen::type_descriptor& type)
	{
		std::size_t inherited = 0;
		for (const reflgen::base_info& base : type.bases())
		{
			if (const reflgen::type_descriptor* descriptor = base.descriptor())
			{
				inherited += descriptor->fields().size();
			}
		}
		return type.fields().subspan(inherited);
	}

	// 이 타입이 선언한 메서드 — LocalFields 와 같은 규칙이다.
	inline std::span<const reflgen::method_info> LocalMethods(const reflgen::type_descriptor& type)
	{
		std::size_t inherited = 0;
		for (const reflgen::base_info& base : type.bases())
		{
			if (const reflgen::type_descriptor* descriptor = base.descriptor())
			{
				inherited += descriptor->methods().size();
			}
		}
		return type.methods().subspan(inherited);
	}

	// type 의 새 인스턴스를 Base 로 — type 이 기본 생성 가능하고 Base 의 반영된 자손일 때만. 포인터 보정은 서술자의
	// base 체인이 한다(다중 상속에서도 맞다). Base 는 가상 소멸자를 가져야 한다(삭제가 Base* 로 일어난다).
	template<class Base>
	std::unique_ptr<Base> Create(const reflgen::type_descriptor& type)
	{
		static_assert(!gc::managed_type<Base>, "Managed objects require an explicit domain factory");
		static_assert(std::has_virtual_destructor_v<Base>, "Create<Base>: Base 는 가상 소멸자가 있어야 한다");
		if (!type.is_constructible())
		{
			return nullptr;
		}
		reflgen::type_descriptor::instance instance = type.create();
		void* base = type.upcast(instance.get(), ToTypeID(TypeTrait::GUIDCreator::GetTypeID<Base>()));
		if (nullptr == base)
		{
			return nullptr;
		}
		instance.release();
		return std::unique_ptr<Base>(static_cast<Base*>(base));
	}

	// 다형 객체의 실타입 주소 — 서술자의 필드 address()·메서드 invoke() 가 받는 그 타입의 포인터다.
	template<class Polymorphic>
	void* MostDerived(Polymorphic* object)
	{
		return dynamic_cast<void*>(object);
	}
}
