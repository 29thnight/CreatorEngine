#pragma once
#define UNUSE_MONO_LIB
#ifndef UNUSE_MONO_LIB
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/object.h>

namespace
{
	template<typename T>
	inline T* FromIntPtr([[maybe_unused]] MonoObject* /*instance*/, intptr_t nativePtr) noexcept
	{
		return reinterpret_cast<T*>(nativePtr);
	}
}
#endif // !UNUSE_MONO_LIB
