#pragma once
// C++ 모듈 프로브 공용 단정.
//
// 판정은 종료 코드다. 실패 사유는 stderr 로, 성공 표식은 stdout 으로 낸다 —
// Tools/regression 의 프로브들과 같은 규약이다. 스크립트는 종료 코드와 표식을
// **둘 다** 본다. 표식만 보면 중간에 죽은 프로세스의 잔여 출력을 성공으로 읽고,
// 종료 코드만 보면 main 에 닿기도 전에 0 으로 끝난 것을 성공으로 읽는다.
//
// ★ STL 헤더만 연다. 모듈 프로브가 이 파일을 import 앞에서 열어도 엔진 헤더가
//   끌려오지 않아야 "모듈로만 본 번역 단위" 가 정말 모듈로만 본다.
#include <cstdio>

namespace probe
{
	inline int g_checks = 0;
	inline int g_failures = 0;

	inline void check(bool condition, const char* what)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::fprintf(stderr, "FAIL: %s\n", what);
		}
	}

	inline int finish(const char* ok_marker)
	{
		std::printf("%d checks, %d failures\n", g_checks, g_failures);
		if (g_failures == 0)
		{
			std::printf("%s=true\n", ok_marker);
			return 0;
		}
		return 1;
	}
}
