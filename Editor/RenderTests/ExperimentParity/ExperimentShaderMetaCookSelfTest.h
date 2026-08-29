#pragma once

#include <string>

namespace RenderTest
{
    // ShaderMeta cook producer 의 **합성** 검사. 프로젝트 자산을 읽지 않고
    // 임시 asset root 를 직접 만들어 굽는다.
    //
    // ★ 실자산 6개는 전부 정상이라 **거부 경로를 하나도 태우지 못한다.**
    //   schema 위반·source 누락·source 의 asset root 탈출은 실자산으로는
    //   영원히 안 나오는 형태이고, 바로 그것이 나중에 조용히 깨지는 자리다.
    //
    // ★ 그리고 이 producer 는 `ShaderMetaLoader::Parse` 를 정본 검증기로
    //   부른다. 그 호출이 실제로 걸러 내는지를 여기서 확인하지 않으면,
    //   "정본을 쓴다"는 주석만 남고 검증은 안 도는 상태가 될 수 있다.
    [[nodiscard]] bool RunExperimentShaderMetaCookSelfTest(std::string& outLog);

    // 실자산 하나. artifact 가 원본과 비트 단위로 같고, source 셰이더 GUID 가
    // 실제 `.hlsl.meta` 와 일치하는지 본다.
    [[nodiscard]] bool RunExperimentShaderMetaCookReal(
        const std::string& assetRootPath, const std::string& shaderMetaPath,
        std::string& outLog);
}
