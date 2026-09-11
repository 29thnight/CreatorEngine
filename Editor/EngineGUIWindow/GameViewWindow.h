#pragma once
#include "ImGui.h"

// PHASE 21 W3: 소유자가 없다. 이 창은 상태를 하나도 들지 않아
// 클래스가 남을 이유가 없었다 — 본문은 자유 함수이고 선언이 그것을 직접
// 가리킨다. 애니메이터 창 셋이 먼저 간 길이다(EditorAnimatorWindows.h).
//
// 진입점 `editor::windows::draw_game_view` 의 선언은
// `EditorWindow/Windows/EditorViewportWindows.h` 가 들고, 정의가 여기 짝인
// `.cpp` 에 있다. 선언과 정의의 의존 방향이 한쪽이다.
