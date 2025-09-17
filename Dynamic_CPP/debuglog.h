#pragma once
#include <iostream>

// Log 사용 시 주석 해제
// Log 끌 시 주석 처리
#define ENABLE_MY_LOG

#ifdef ENABLE_MY_LOG
      // 로그가 켜져 있을 때의 동작
#define LOG(...) std::cout << "[" << __func__ << "] " << __VA_ARGS__ << std::endl
#else
    // 로그가 꺼져 있을 때의 동작 (아무것도 안 함)
#define LOG(...) do {} while(0)
#endif