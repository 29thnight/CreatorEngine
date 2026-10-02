// ce.core 를 **import 로만** 보는 번역 단위.
//
// 판정하는 것은 둘이다.
//   ① 값    — Uuid 의 바이트 동일성 계약(RFC 4122 v5 공표 벡터, 파싱 거절 집합)이
//             import 경로에서도 그대로인가. 이 값은 .meta 와 씬이 참조한다.
//   ② 동일성 — 헤더 쪽과 같은 엔터티인가. 타입(typeid)과, 결정적인 것 —
//             Singleton<T>::s_instance 가 하나인가.
//
// 판정은 종료 코드와 표식 CPP_MODULE_CORE_OK 다.
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>

#include "probe_check.h"

import ce.core;

#include "core_probe_singleton.h"
#include "core_peer.h"

namespace
{
	using probe::check;

	const Uuid::Uuid16 kDns = Uuid::Parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8");

	void test_uuid_values()
	{
		// RFC 4122 v5 공표 벡터 — uuid5(NAMESPACE_DNS, "www.example.com").
		const Uuid::Uuid16 value = Uuid::FromName(kDns, "www.example.com");
		check(Uuid::ToString(value) == "2ed6657d-e927-568b-95e1-2665a8aea6a2",
		      "uuid/v5-vector — import 경로의 v5 값이 공표 벡터와 같다");

		// 숨은 friend 연산자가 ADL 로 보이는가.
		check(Uuid::Nil() == Uuid::Uuid16{}, "uuid/equal — operator== 가 보인다");
		check((value <=> Uuid::Nil()) != 0, "uuid/order — operator<=> 가 보인다");
		check(Uuid::Nil().IsNil(), "uuid/nil — Nil 은 비어 있다");

		// 거절 집합이 같은가(Uuid.h 머리말의 하이픈 규칙).
		Uuid::Uuid16 out;
		check(!Uuid::TryParse("0123-456789abcdef0123456789abcdef", out),
		      "uuid/reject — 엉뚱한 자리의 하이픈을 거절한다");
		check(Uuid::TryParse("{6BA7B810-9DAD-11D1-80B4-00C04FD430C8}", out) && out == kDns,
		      "uuid/accept-braced — 대문자 중괄호 형태를 받는다");

		bool threw = false;
		try
		{
			(void)Uuid::Parse("not-a-uuid");
		}
		catch (const std::runtime_error&)
		{
			threw = true;
		}
		check(threw, "uuid/throw — 던지는 파싱이 runtime_error 를 던진다");
	}

	void test_identity()
	{
		check(typeid(Uuid::Uuid16) == probe::core_peer::uuid16_type(),
		      "identity/type-uuid — import 와 헤더가 같은 Uuid16 을 본다");
		check(Uuid::FromName(kDns, "Assets/Scene.creator") ==
		          probe::core_peer::v5_from_header("Assets/Scene.creator"),
		      "identity/v5 — 같은 입력이 경계를 넘어 같은 값");

		// ★ 이 파일의 핵심 검사. s_instance 가 둘이면 헤더 쪽과 모듈 쪽이 각자
		//   인스턴스를 만든다 — 엔진의 SceneManager · DataSystem 이 둘이 되는 일이다.
		CppModuleProbeSingleton* fromModule = CppModuleProbeSingleton::GetInstance();
		check(fromModule == probe::core_peer::singleton_from_header(),
		      "identity/singleton — 싱글턴 인스턴스가 하나다");

		fromModule->value = 7;
		check(probe::core_peer::singleton_value_from_header() == 7,
		      "identity/singleton-value — 모듈 쪽에서 쓴 값을 헤더 쪽이 읽는다");

		probe::core_peer::destroy_singleton_from_header();
		check(!CppModuleProbeSingleton::IsAlive(),
		      "identity/singleton-destroy — 헤더 쪽 파괴가 모듈 쪽에서 보인다");
	}
}

int main()
{
	test_uuid_values();
	test_identity();
	return probe::finish("CPP_MODULE_CORE_OK");
}
