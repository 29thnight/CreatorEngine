#pragma once
#if defined(_MSC_VER)
#define CE_NOINLINE __declspec(noinline)
#define CE_FORCEINLINE __forceinline
#else
#define CE_NOINLINE __attribute__((noinline))
#define CE_FORCEINLINE inline __attribute__((always_inline))
#endif
#if defined(_MSC_VER) && defined(__clang__)
#define CE_NO_UNIQUE_ADDRESS
#elif defined(_MSC_VER)
#define CE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define CE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
