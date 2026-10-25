#pragma once

// Windows SDK 계열 헤더가 legacy near/far 매크로를 남긴 TU에서도 upstream
// Mathematics 헤더를 그대로 포함할 수 있게 include 구간만 매크로를 격리한다.
// 매크로 상태는 즉시 복구하므로 기존 Windows 소비자의 전처리 환경은 바꾸지 않는다.
#if defined(near)
#pragma push_macro("near")
#undef near
#define CREATOR_MATHEMATICS_RESTORE_NEAR 1
#endif

#if defined(far)
#pragma push_macro("far")
#undef far
#define CREATOR_MATHEMATICS_RESTORE_FAR 1
#endif

#include <mathematics/intersect.hpp>

#if defined(CREATOR_MATHEMATICS_RESTORE_FAR)
#pragma pop_macro("far")
#undef CREATOR_MATHEMATICS_RESTORE_FAR
#endif

#if defined(CREATOR_MATHEMATICS_RESTORE_NEAR)
#pragma pop_macro("near")
#undef CREATOR_MATHEMATICS_RESTORE_NEAR
#endif
