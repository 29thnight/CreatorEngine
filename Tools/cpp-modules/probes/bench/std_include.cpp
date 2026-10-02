// 벤치 — 엔진 공용 헤더 사슬이 실어 나르는 STL 묶음을 #include 로 연다.
//
// 목록은 Core.Definition.h(STL 26 개) · LogSystem.h · PathFinder.h · TypeTrait.h ·
// Uuid.h 가 전이로 여는 표준 헤더의 합집합이다. 정적 분석으로 이 묶음은 427 개
// 번역 단위 중 약 180~400 개에 닿는다. 엔진 헤더(windows.h · spdlog)는 넣지
// 않았다 — 여기서 재는 것은 표준 라이브러리를 들이는 비용 하나다.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "std_body.inl"
