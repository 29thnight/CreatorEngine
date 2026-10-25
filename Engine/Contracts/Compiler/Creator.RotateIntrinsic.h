#pragma once
#if defined(_MSC_VER)
// Compiler intrinsic declaration only. Keep CRT type definitions out of the
// global module fragment, where they would collide with SDK header units.
extern "C" unsigned int __cdecl _rotr(unsigned int, int);
#pragma intrinsic(_rotr)
#endif
