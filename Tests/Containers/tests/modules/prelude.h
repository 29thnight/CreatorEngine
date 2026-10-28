#pragma once
// Test-only standard declarations. No CE implementation header is included.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif
#if defined(_MSC_VER)
#include <concurrent_queue.h>
#define CE_NOINLINE __declspec(noinline)
#else
#define CE_NOINLINE __attribute__((noinline))
#endif
