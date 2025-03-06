#pragma once
#include <cstdlib>
#include <cstdio>
#include <Windows.h>
//EASTL의 ASSERT를 모방
namespace Core
{
    // Assertion 실패 시 호출될 함수 포인터
    using AssertionFailureFunction = void (*)(const char* expression, void* context);

    // 기본 Assertion 실패 처리 함수
    inline void AssertionFailureFunctionDefault(const char* expression, void* context)
    {
        // 기본적으로 오류 메시지 출력
        printf("Assertion failed: %s\n", expression);

        // 디버깅 환경에서 중단
#if defined(_DEBUG)
        if (::IsDebuggerPresent())
        {
            OutputDebugStringA(expression);  // 디버깅 도구에 메시지 출력
        }
        __debugbreak();  // 디버거 중단
#endif

        // 프로그램 종료
        std::abort();
    }

    // 전역 변수로 Assertion 실패 함수와 컨텍스트 저장
    static AssertionFailureFunction g_assertionFailureFunction = AssertionFailureFunctionDefault;
    static void* g_assertionFailureFunctionContext = nullptr;

    // Assertion 실패 함수 설정
    inline void SetAssertionFailureFunction(AssertionFailureFunction failureFunction, void* context)
    {
        g_assertionFailureFunction = failureFunction;
        g_assertionFailureFunctionContext = context;
    }

    // Assertion 실패 처리 함수 호출
    inline void AssertionFailure(const char* expression)
    {
        if (g_assertionFailureFunction)
        {
            g_assertionFailureFunction(expression, g_assertionFailureFunctionContext);
        }
    }
}

// CORE_ASSERT 함수 정의
#ifndef CORE_ASSERT
#ifdef NDEBUG
#define CORE_ASSERT(expression) ((void)0)  // NDEBUG에서 assert 비활성화
#else
#define CORE_ASSERT(expression) \
    if (!(expression))           \
    {                            \
        Core::AssertionFailure(#expression); \
    }
#endif
#endif // !CORE_ASSERT
