#ifndef DYNAMICCPP_EXPORTS
#include "RHIShaderSource.h"

#include "../../Utility_Framework/PathFinder.h"

#include <fstream>
#include <sstream>

std::filesystem::path RHIShaderSource::Resolve(std::string_view name)
{
    return PathFinder::RelativeToShader(kFolder) / name;
}

bool RHIShaderSource::Load(std::string_view name, std::string& outText, std::string& outError)
{
    const std::filesystem::path path = Resolve(name);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        // ★ 경로를 통째로 넣는다. "셰이더를 못 찾았다"만 나오면 배포가
        //   잘못된 것인지 이름이 틀린 것인지 가릴 수 없다.
        outError = "패스 셰이더를 찾을 수 없다: " + path.string();
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        outError = "패스 셰이더를 열 수 없다: " + path.string();
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    outText = buffer.str();

    if (outText.empty())
    {
        outError = "패스 셰이더가 비었다: " + path.string();
        return false;
    }

    return true;
}

#endif
