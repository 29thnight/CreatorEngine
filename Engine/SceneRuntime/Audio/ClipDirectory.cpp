#include "ClipDirectory.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_set>

namespace wave
{
    namespace
    {
        std::string LowerExtension(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return extension;
        }
    }

    bool IsSupportedClipExtension(const std::filesystem::path& path)
    {
        const std::string extension = LowerExtension(path);
        return ".wav" == extension || ".mp3" == extension || ".flac" == extension;
    }

    ClipScanReport LoadClipsFromDirectory(AudioService& service,
        const std::filesystem::path& directory)
    {
        ClipScanReport report{};

        std::error_code error{};
        if (!std::filesystem::is_directory(directory, error)) return report;

        // 이미 적재된 것을 먼저 모은다. 두 번 훑어도 같은 결과여야 하고, 같은
        // 이름이 두 번 오면 **뒤엣것을 버렸다는 사실이 남아야** 한다.
        std::unordered_set<ClipKey> known;
        for (const ClipKey& key : service.ListClipKeys()) known.insert(key);

        // ★ 예외를 던지는 순회를 쓰지 않는다. 권한 없는 하위 폴더 하나가 순회
        //   전체를 끊으면 "폴더가 비었다" 와 구분되지 않는다.
        std::filesystem::recursive_directory_iterator iterator(
            directory, std::filesystem::directory_options::skip_permission_denied, error);
        if (error) return report;

        const std::filesystem::recursive_directory_iterator end{};
        for (; iterator != end; iterator.increment(error))
        {
            if (error) break;

            const std::filesystem::directory_entry& entry = *iterator;
            if (entry.is_directory(error)) continue;

            const std::filesystem::path& path = entry.path();
            if (!IsSupportedClipExtension(path))
            {
                ++report.unsupported;
                continue;
            }

            // 키는 확장자를 뗀 파일 이름이다.
            //
            // ★ 이것은 AU2 까지의 한시적 규칙이다. 자산 identity 가 `.meta` 로
            //   서면 키는 `AssetId` 가 되고 폴더 구조와 무관해진다. 그때까지는
            //   같은 이름이 두 폴더에 있으면 하나가 진다 — 지금은 그 사실을
            //   세어서 드러낸다.
            const ClipKey key{ path.stem().string() };
            if (known.find(key) != known.end())
            {
                ++report.collided;
                report.collidedKeys.push_back(key.Text());
                continue;
            }

            if (!service.LoadClip(key, path))
            {
                ++report.rejected;
                report.rejectedFiles.push_back(path.filename().string());
                continue;
            }

            known.insert(key);
            ++report.accepted;
        }

        return report;
    }
}
