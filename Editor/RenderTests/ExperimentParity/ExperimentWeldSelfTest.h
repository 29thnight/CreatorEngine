#pragma once

#include <string>

namespace RenderTest
{
    // 정점 용접(I4)의 **합성** 검사. 자산을 읽지 않는다.
    //
    // ★ 존재 이유가 특이하다. 실자산에서 이 패스는 **한 개도 합치지 않는다** —
    //   fastgltf·ufbx 는 이미 인덱싱된 소스를 그대로 읽어서 중복 정점이 없다
    //   (legacy 가 aiProcess_JoinIdenticalVertices 를 켜는 이유는 Assimp 자체
    //   로더가 중복을 만들기 때문이다).
    //
    //   그래서 실자산 게이트만 보면 **"0개 합쳤다"와 "패스가 아예 안 돌았다"를
    //   구분할 수 없다.** 둘 다 정점 수가 그대로다. 중복을 손으로 만들어
    //   넣어야 비로소 이 패스가 살아 있는지 알 수 있다.
    //
    // ★ 그리고 "합치면 안 되는 것을 안 합치는가"가 합치는 것만큼 중요하다.
    //   uv1·탄젠트·skin weight 만 다른 정점을 합치면 화면에 이음매로 나타나고
    //   원인을 찾기 어렵다. 키가 ValueStreams() 전부를 덮는지 여기서 본다.
    [[nodiscard]] bool RunExperimentWeldSelfTest(std::string& outLog);
}
