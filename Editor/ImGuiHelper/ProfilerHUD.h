#pragma once

// 프로파일러 HUD 표시 계약. 수집 코어(EngineDiagnostics/Profiler.h)에서
// 분리했다(P1a) — ImGui는 수집 데이터의 reader일 뿐이고, 코어는 표시를
// 모른다. 구현은 ProfilerWindow.cpp.
#include <mutex>

// 불변 capture 표시는 씬을 붙들지 않고, live 페이지는 같은 수명 잠금을 잠깐 취한다.
void DrawProfilerHUD(std::mutex& sceneStructureMutex);
