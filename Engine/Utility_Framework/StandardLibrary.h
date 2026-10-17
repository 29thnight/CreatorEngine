#pragma once

// Only source-analysis tools and isolated non-module probes use classic headers.
#if defined(CE_CONTRACT_STANDALONE)
#if __has_include(<algorithm>)
#include <algorithm>
#endif
#if __has_include(<any>)
#include <any>
#endif
#if __has_include(<array>)
#include <array>
#endif
#if __has_include(<atomic>)
#include <atomic>
#endif
#if __has_include(<barrier>)
#include <barrier>
#endif
#if __has_include(<bit>)
#include <bit>
#endif
#if __has_include(<bitset>)
#include <bitset>
#endif
#if __has_include(<cctype>)
#include <cctype>
#endif
#if __has_include(<cerrno>)
#include <cerrno>
#endif
#if __has_include(<cfloat>)
#include <cfloat>
#endif
#if __has_include(<charconv>)
#include <charconv>
#endif
#if __has_include(<chrono>)
#include <chrono>
#endif
#if __has_include(<cinttypes>)
#include <cinttypes>
#endif
#if __has_include(<climits>)
#include <climits>
#endif
#if __has_include(<cmath>)
#include <cmath>
#endif
#if __has_include(<compare>)
#include <compare>
#endif
#if __has_include(<complex>)
#include <complex>
#endif
#if __has_include(<concepts>)
#include <concepts>
#endif
#if __has_include(<condition_variable>)
#include <condition_variable>
#endif
#if __has_include(<coroutine>)
#include <coroutine>
#endif
#if __has_include(<csignal>)
#include <csignal>
#endif
#if __has_include(<cstddef>)
#include <cstddef>
#endif
#if __has_include(<cstdint>)
#include <cstdint>
#endif
#if __has_include(<cstdio>)
#include <cstdio>
#endif
#if __has_include(<cstdlib>)
#include <cstdlib>
#endif
#if __has_include(<cstring>)
#include <cstring>
#endif
#if __has_include(<ctime>)
#include <ctime>
#endif
#if __has_include(<cwchar>)
#include <cwchar>
#endif
#if __has_include(<cwctype>)
#include <cwctype>
#endif
#if __has_include(<deque>)
#include <deque>
#endif
#if __has_include(<exception>)
#include <exception>
#endif
#if __has_include(<execution>)
#include <execution>
#endif
#if __has_include(<expected>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <expected>
#endif
#if __has_include(<filesystem>)
#include <filesystem>
#endif
#if __has_include(<format>)
#include <format>
#endif
#if __has_include(<fstream>)
#include <fstream>
#endif
#if __has_include(<functional>)
#include <functional>
#endif
#if __has_include(<future>)
#include <future>
#endif
#if __has_include(<initializer_list>)
#include <initializer_list>
#endif
#if __has_include(<iomanip>)
#include <iomanip>
#endif
#if __has_include(<iosfwd>)
#include <iosfwd>
#endif
#if __has_include(<iostream>)
#include <iostream>
#endif
#if __has_include(<istream>)
#include <istream>
#endif
#if __has_include(<iterator>)
#include <iterator>
#endif
#if __has_include(<latch>)
#include <latch>
#endif
#if __has_include(<limits>)
#include <limits>
#endif
#if __has_include(<list>)
#include <list>
#endif
#if __has_include(<locale>)
#include <locale>
#endif
#if __has_include(<map>)
#include <map>
#endif
#if __has_include(<memory>)
#include <memory>
#endif
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif
#if __has_include(<mutex>)
#include <mutex>
#endif
#if __has_include(<new>)
#include <new>
#endif
#if __has_include(<numeric>)
#include <numeric>
#endif
#if __has_include(<optional>)
#include <optional>
#endif
#if __has_include(<ostream>)
#include <ostream>
#endif
#if __has_include(<queue>)
#include <queue>
#endif
#if __has_include(<random>)
#include <random>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#if __has_include(<regex>)
#include <regex>
#endif
#if __has_include(<semaphore>)
#include <semaphore>
#endif
#if __has_include(<set>)
#include <set>
#endif
#if __has_include(<shared_mutex>)
#include <shared_mutex>
#endif
#if __has_include(<source_location>)
#include <source_location>
#endif
#if __has_include(<span>)
#include <span>
#endif
#if __has_include(<sstream>)
#include <sstream>
#endif
#if __has_include(<stack>)
#include <stack>
#endif
#if __has_include(<stdexcept>)
#include <stdexcept>
#endif
#if __has_include(<stop_token>)
#include <stop_token>
#endif
#if __has_include(<string>)
#include <string>
#endif
#if __has_include(<string_view>)
#include <string_view>
#endif
#if __has_include(<system_error>)
#include <system_error>
#endif
#if __has_include(<thread>)
#include <thread>
#endif
#if __has_include(<tuple>)
#include <tuple>
#endif
#if __has_include(<type_traits>)
#include <type_traits>
#endif
#if __has_include(<typeindex>)
#include <typeindex>
#endif
#if __has_include(<typeinfo>)
#include <typeinfo>
#endif
#if __has_include(<unordered_map>)
#include <unordered_map>
#endif
#if __has_include(<unordered_set>)
#include <unordered_set>
#endif
#if __has_include(<utility>)
#include <utility>
#endif
#if __has_include(<variant>)
#include <variant>
#endif
#if __has_include(<vector>)
#include <vector>
#endif
#if __has_include(<flat_map>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <flat_map>
#endif
#if __has_include(<flat_set>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <flat_set>
#endif
#if __has_include(<generator>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <generator>
#endif
#if __has_include(<mdspan>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <mdspan>
#endif
#if __has_include(<print>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <print>
#endif
#if __has_include(<stacktrace>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <stacktrace>
#endif
#if __has_include(<stdfloat>) && (__cplusplus > 202002L || (defined(_MSVC_LANG) && _MSVC_LANG > 202002L))
#include <stdfloat>
#endif
#else
#include <yvals_core.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <vcruntime_new.h>
#include <corecrt_startup.h>
#include <signal.h>
#include <intrin.h>
import std;
// Preserve the legacy global alias supplied by MSVC's <cstddef> for SDK clients.
using std::max_align_t;
#endif
