// 헤더 자급자족 프로브.
//
// 과거 함정: Scene.h를 첫 include로 만나는 TU는 깨졌다 —
//   Scene.h → (전이) → Entity.h → Entity.inl → Scene.h
// 재진입 순환에서 pragma once가 두 번째 Scene.h를 건너뛰어, Entity.inl이
// 아직 선언되지 않은 Scene 멤버를 쓰다 실패했다. 그래서 "Scene.h보다
// Entity.h를 먼저 include하라"는 부족지식이 필요했다.
//
// 지금은 Scene.h가 Entity를 온전히 포함하되 Entity.inl이 Scene.h를 물지 않아
// 순환이 없다. 이 TU는 그 상태를 상시 검증한다 — 누군가 Entity.inl에서 Scene.h를
// 다시 닿게 만들면 여기가 가장 먼저 깨진다.
//
// ★ 이 파일은 유니티 빌드에서 제외된다(IncludeInUnityFile=false).
//   블롭에 합쳐지면 앞 파일들의 include가 전이되어 프로브가 무의미해진다.
#include "Scene.h"
