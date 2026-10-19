#pragma once
// PrSM 값 타입(D-042)의 동의 목록 — 엔진이 스크립트에 값으로 넘기는 타입은 여기 별칭 하나씩이다.
//
// 대상(서드파티 Mathematics 라이브러리·RenderEngine 열거형)의 header 는 건드리지 않는다.
// reflgen 이 이 별칭에서 대상의 필드·오프셋·열거자를 읽고, 생성 브리지가 실제 선언과
// 크기·정렬·오프셋·열거자 값을 static_assert 로 맞춘다. 맞지 않으면 SceneRuntime 이
// 컴파일되지 않는다.
//
//   레코드(Vector3·Quaternion·Color)  스크립트의 구조체 — 생성자와 필드(x, y, z …)를 쓴다.
//                                     C 래퍼에는 바이트 그대로 포인터로 넘어간다.
//   열거형(LightType)                  스크립트의 enum — creator::LightType::SpotLight.
//                                     경계에는 선언 순서의 case 번호로 넘어간다.
//
// 별칭 이름은 ABI 정체성이 아니다. 스크립트 이름은 value("...") 가 정한다.
#include <mathematics/color.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector3.hpp>
#include "LightProperty.h"

namespace prsm_values
{
    using Vector3 [[reflgen::interop("prsm"), reflgen::value("creator::Vector3")]] = math::vector3;
    using Quaternion [[reflgen::interop("prsm"), reflgen::value("creator::Quaternion")]] = math::quaternion;
    using Color [[reflgen::interop("prsm"), reflgen::value("creator::Color")]] = math::color;
    using LightType [[reflgen::interop("prsm"), reflgen::value("creator::LightType")]] = ::LightType;
} // namespace prsm_values
