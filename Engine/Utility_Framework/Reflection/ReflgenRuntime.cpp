// 등록소 정의를 여기 둔다 — 헤더 인라인 싱글턴은 모듈(PE)마다 갈린다(ReflectionYml.cpp 의 OpsRegistry 와 같은 이유).
#include "ReflgenRuntime.h"

namespace Meta
{
	reflgen::registry& Types()
	{
		static reflgen::registry s_types;
		return s_types;
	}
}
