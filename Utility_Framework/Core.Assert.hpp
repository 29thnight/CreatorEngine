#pragma once
#include <cstdlib>
#include <cstdio>
#include <Windows.h>
namespace Core
{
    using AssertionFailureFunction = void (*)(const char* expression, void* context);

    inline void AssertionFailureFunctionDefault(const char* expression, void* context)
    {
        printf("Assertion failed: %s\n", expression);

#if defined(_DEBUG)
        if (::IsDebuggerPresent())
        {
            OutputDebugStringA(expression);
        }
        __debugbreak();
#endif
        std::abort();
    }

    static AssertionFailureFunction g_assertionFailureFunction = AssertionFailureFunctionDefault;
    static void* g_assertionFailureFunctionContext = nullptr;

    inline void SetAssertionFailureFunction(AssertionFailureFunction failureFunction, void* context)
    {
        g_assertionFailureFunction = failureFunction;
        g_assertionFailureFunctionContext = context;
    }

    inline void AssertionFailure(const char* expression)
    {
        if (g_assertionFailureFunction)
        {
            g_assertionFailureFunction(expression, g_assertionFailureFunctionContext);
        }
    }
}

#ifndef CORE_ASSERT
#ifdef NDEBUG
#define CORE_ASSERT(expression) ((void)0)
#else
#define CORE_ASSERT(expression) \
    if (!(expression))           \
    {                            \
        Core::AssertionFailure(#expression); \
    }
#endif
#endif // !CORE_ASSERT
