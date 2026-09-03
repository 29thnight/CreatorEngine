#pragma once

#include <string>

namespace RenderTest
{
    // I5-D2 (V4) — 마스크→RHIInputElement 유도의 합성 검사.
    //
    // ① 유도가 표(kVertexAttributeTable)와 자기일관인지(시맨틱·포맷·오프셋·
    //    stride), ② 지원 밖 마스크가 fail-closed인지, ③ legacy ::Vertex(96B)
    //    대비 전환 차이(BINORMAL 부재·TANGENT RGBA32·BLENDINDICES RGBA8Uint·
    //    오프셋 이동)를 명시 단정한다 — 이 차이 명세가 D4(정점 버퍼와 동시
    //    전환)가 지켜야 할 계약이다.
    [[nodiscard]] bool RunExperimentVertexLayoutSelfTest(std::string& outLog);
    [[nodiscard]] bool RunModelRenderWiringSelfTest(std::string& outLog);
}
