#pragma once

// Export macro for the single GC runtime. Every DLL and translation unit must
// share one runtime instance, so the non-template state lives in one library.
#if defined(GCCE_SHARED)
#    if defined(_WIN32)
#        if defined(GCCE_BUILDING)
#            define GC_API __declspec(dllexport)
#        else
#            define GC_API __declspec(dllimport)
#        endif
#    else
#        define GC_API __attribute__((visibility("default")))
#    endif
#else
#    define GC_API
#endif

// Debug checks: owner-thread checks for root registration, reference
// stores, allocation and collection, and detection of misuse (roots inside GC
// objects, GC types created outside gc::make, weak references that outlive
// their domain). On by default in debug builds, off in release builds.
#if !defined(GC_DEBUG_CHECKS)
#    if defined(NDEBUG)
#        define GC_DEBUG_CHECKS 0
#    else
#        define GC_DEBUG_CHECKS 1
#    endif
#endif

// GCCE requires C++23 (deducing this is used by gc::managed).
// Clang 18 implements explicit object parameters without defining the
// feature-test macro, so it is accepted by version.
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
#elif defined(__clang__) && __clang_major__ >= 18 && __cplusplus > 202002L
#elif defined(_MSC_VER) && !defined(__clang__) && _MSC_VER >= 1932 && defined(_MSVC_LANG) && _MSVC_LANG > 202002L
#else
#    error "GCCE requires C++23 with explicit object parameters (GCC 14, Clang 18, MSVC 19.32 or newer)"
#endif
