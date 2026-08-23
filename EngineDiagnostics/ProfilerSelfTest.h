#pragma once
#include <string>

// 현행 CPU 프로파일러의 관측 가능한 계약을 고정하는 특성화 검사(PHASE 14 P0).
//
// 이 검사는 "프로파일러가 좋은가"를 묻지 않는다. **지금 무엇이 참인가**를 못박아,
// P2에서 수집 코어를 갈아끼울 때 무엇이 깨졌는지 즉시 드러나게 한다.
// 그래서 이미 알고 있는 결함(프레임을 넘는 스코프의 유실 등)은 실패로 세지 않고
// KNOWN-DEFECT로 따로 센다 — P2 이후 그 항목이 PASS로 바뀌는 것이 진척의 정의다.
//
// 주의: 전역 gCPUProfiler를 상대로 돌고 프레임 경계(Tick)를 스스로 넘긴다.
// 즉 **라이브 캡처를 교란한다**. 성능을 재는 중에 부르지 말 것.
// 반드시 게임 스레드(ConsoleCommandSystem::Pump)에서만 호출한다.
//
// 반환: 실패 0이면 true. outLog에 항목별 기대값·실측값과 프로파일러 자체 비용이 담긴다.
bool RunProfilerSelfTest(std::string& outLog);

// 프로파일러 자체 비용·용량 소진 현황만 문자열로 뽑는다(profile.stats).
// 검사와 달리 프레임을 넘기지 않으므로 라이브 캡처를 건드리지 않는다.
std::string GetProfilerStatsReport();
