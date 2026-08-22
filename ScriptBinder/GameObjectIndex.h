#pragma once
// Entity::Index의 독립 정의 (PHASE 4-2 C2).
//
// 인덱스 타입 하나가 필요한 헤더가 Entity.h 전체를 물지 않게 하는 경량
// 정의다. Entity::Index는 이 별칭을 가리킨다.
// (과거의 Scene.h ↔ Entity.h 순환은 Entity.inl의 Scene.h include를
//  EntityAt 우회로 제거하며 근본 해소됐다 — Scene.h 상단 주석 참고.)
using GameObjectIndex = int;
