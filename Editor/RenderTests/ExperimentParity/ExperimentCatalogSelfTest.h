#pragma once

#include <string>

namespace RenderTest
{
    // `CookedAssetCatalog` 의 합성 검사. 손으로 만든 manifest 로 조회·폐포·
    // 거부를 본다.
    [[nodiscard]] bool RunExperimentCatalogSelfTest(std::string& outLog);

    // ★ **전수 해석 증명.** 실제 Derived tree 의 CEMF 를 읽어 catalog 를 세우고,
    //   그 안의 **모든 GUID** 에 대해 확인한다.
    //
    //     1. 조회가 되는가
    //     2. artifact 파일이 실재하고 크기·해시가 manifest 와 같은가
    //     3. 모든 dependency 가 catalog 안에서 해소되는가
    //     4. 각 entry 의 폐포가 위상 순서로 나오는가(의존이 자기보다 먼저)
    //
    //   쿠커의 폐포 스윕은 **게시 직전 staging** 을 본다. 이것은 **게시된 tree** 를
    //   소비자 관점에서 다시 본다 — 같은 성질을 다른 쪽에서 재는 것이라
    //   staging 과 게시본이 갈라지면 여기서 드러난다.
    //
    //   derivedRoot 는 `Derived/` 의 부모 디렉터리다.
    [[nodiscard]] bool RunExperimentCatalogReal(
        const std::string& derivedRootPath, std::string& outLog);
}
