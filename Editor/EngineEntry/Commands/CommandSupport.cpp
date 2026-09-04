#include "CommandSupport.h"

// ★ 이 TU 는 유니티 빌드에서 빠져 있다(`IncludeInUnityFile=false`).
//
//   그래서 include 를 스스로 소유해야 한다 — 다른 파일이 앞서 들여온 헤더에
//   기대면 청크가 재편될 때 조용히 깨진다. §12 가 "include 는 각 TU 가 직접
//   소유한다"고 못 박은 이유이고, 유니티에서 빼 두면 그 규칙이 **평상시
//   빌드마다** 검사된다. 아무도 안 돌리는 별도 빌드 모드에 맡기지 않는다.
#include "PathFinder.h"

#include <filesystem>
#include <system_error>

namespace ConsoleCmd
{
    std::string ResolveTestArtifactPath(std::string_view category,
                                        std::string_view requestedPath)
    {
        std::filesystem::path output(requestedPath);
        if (output.is_relative())
        {
            output = PathFinder::TestArtifactPath(category) / output;
        }
        output = output.lexically_normal();
        std::error_code error{};
        std::filesystem::create_directories(output.parent_path(), error);
        return output.string();
    }

    std::string TrimLine(const std::string& text)
    {
        static constexpr const char* kBlank = " \t\r\n";
        const auto begin = text.find_first_not_of(kBlank);
        if (begin == std::string::npos) return {};
        const auto end = text.find_last_not_of(kBlank);
        return text.substr(begin, end - begin + 1);
    }
}
