// reflgen 전환 동등성 증명 — Tools/migration/reflgen_codemod.py 가 옮기기 전의 레시피에서 썼다.
//
// 옮긴 타입마다 다리(ReflgenBridge.h)가 만든 엔진 스키마가 옛 레시피와 필드 이름·순서·속성 타입·속성 값·
// 메서드·파라미터 이름까지 같은지 컴파일 때 단정한다. 엔진 소비자는 스키마 타입에 대한 template 이라 같으면
// 동작도 같다. 이 표는 옛 레시피의 기록이다 — 필드를 더하거나 빼면 여기 줄도 고친다.
#include "ReflgenParity.h"
#include "InspectorLayoutFixture.h"

namespace
{
    static_assert(parity::matches<editor::inspector::InspectorFixtureLeaf>(
        parity::field("weight"),
        parity::field("note"),
        parity::field("offset")));
    static_assert(parity::matches<editor::inspector::InspectorFixtureBranch>(
        parity::field("depth"),
        parity::field("title"),
        parity::field("leaf"),
        parity::field("samples")));
    static_assert(parity::matches<editor::inspector::InspectorLayoutFixture>(
        parity::field("count"),
        parity::field("ratio"),
        parity::field("toggle"),
        parity::field("caption"),
        parity::field("mode"),
        parity::field("extent"),
        parity::field("position"),
        parity::field("plane"),
        parity::field("branch"),
        parity::field("weights"),
        parity::field("names"),
        parity::field("points"),
        parity::field("smallIds"),
        parity::field("fixed"),
        parity::field("tags"),
        parity::field("scores"),
        parity::field("leaves"),
        parity::field("branches")));
}
