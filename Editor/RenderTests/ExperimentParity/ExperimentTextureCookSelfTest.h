#pragma once

#include <string>

namespace RenderTest
{
    // texture cook producer 의 **합성** 검사. 프로젝트 자산을 읽지 않고
    // 임시 asset root 를 직접 만들어 굽는다.
    //
    // ★ 합성이 필요한 이유가 이 슬라이스에는 특히 뚜렷하다. 실자산 corpus 는
    //   png 98 · hdr 14 뿐이고 **`.dds` 는 하나도 통과하지 못한다**(하나뿐인
    //   `blueNoise.dds` 의 sidecar 가 brace 표기라 fail-closed 로 거부된다).
    //   즉 실자산만으로는 allowlist 세 갈래 중 하나가 아예 안 돈다 — 그 가지를
    //   여기서 태운다.
    //
    // ★ 그리고 거부가 실제로 되는지 본다. "지원하지 않는 확장자는 거부한다"는
    //   적어 두는 것으로는 아무 일도 하지 않는다. 조용히 건너뛰면 그 텍스처를
    //   참조하는 재질이 나중에 폐포 검사에서 "없는 의존"으로 터지는데, 그때는
    //   원인이 여기서 멀어져 있다.
    [[nodiscard]] bool RunExperimentTextureCookSelfTest(std::string& outLog);

    // 실자산 하나를 굽고 artifact 바이트가 원본과 **비트 단위로 같은지** 본다.
    // pass-through artifact 라 이것이 확인할 수 있는 전부이고, 확인할 수 없는
    // 것을 확인한 척하지 않는다.
    [[nodiscard]] bool RunExperimentTextureCookReal(
        const std::string& assetRootPath, const std::string& texturePath,
        std::string& outLog);
}
