#pragma once
// GameObject::Index의 독립 정의 (PHASE 4-2 C2).
//
// Scene.h가 인덱스 타입 하나 때문에 GameObject.h 전체를 요구하면
// Scene.h -> GameObject.h -> GameObject.inl -> IRegistableEvent.h -> Scene.h
// 순환이 닫힌다. 인덱스를 여기로 분리해 Scene.h가 전방 선언 + 이 헤더만으로
// 자급자족하도록 한다. GameObject::Index는 이 별칭을 가리킨다.
using GameObjectIndex = int;
