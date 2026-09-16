#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "AudioService.h"

namespace wave
{
    // 폴더 하나를 훑어 클립을 적재한다. **동기 호출 한 번**이다.
    //
    // ★ 옛 배선은 이 일을 1초 주기 폴링 스레드가 했다. 파일 수가 바뀌면 폴더를
    //   통째로 다시 훑고, 그 스레드의 실행 여부를 종료 경로가 기다렸다 — 클립이
    //   하나도 없으면 플래그가 내려가지 않아 종료가 영원히 멈췄다. 훑는 일을
    //   호출로 만들면 기다릴 스레드가 없다.
    //
    // ★★ 자산 감시(파일이 늘었을 때 다시 훑는 일)는 여기 없다. Editor 는 이미
    //   `efsw` 로 자산을 보고 있고, 오디오가 자기 감시기를 또 드는 것이 폴링
    //   스레드를 낳은 원인이었다. 다시 훑어야 하면 **Host 가 이 함수를 다시**
    //   부른다.
    struct ClipScanReport final
    {
        std::size_t accepted{ 0 };      // 적재에 성공했다
        std::size_t rejected{ 0 };      // 지원 확장자인데 적재가 거부됐다(손상 등)
        std::size_t unsupported{ 0 };   // 확장자가 목록 밖이다(.ogg 포함)
        std::size_t collided{ 0 };      // 이름이 이미 있어 뒤엣것을 버렸다

        // 판정용. 무엇이 왜 빠졌는지 사람이 읽을 수 있어야 한다.
        std::vector<std::string> rejectedFiles;
        std::vector<std::string> collidedKeys;

        [[nodiscard]] std::size_t Examined() const noexcept
        {
            return accepted + rejected + unsupported + collided;
        }
    };

    // 적재 가능한 확장자인가. 비교는 소문자로 한다.
    //
    // ★ `.ogg` 는 **일부러 뺐다.** miniaudio 를 `MA_NO_ENCODING` 으로 들이면서
    //   내장 디코더는 WAV·FLAC·MP3 셋이다. 옛 배선은 `.ogg` 를 목록에 넣어 두고
    //   FMOD 가 실패하면 로그만 남겼다 — 저작자는 "넣었는데 안 난다" 를 보고
    //   원인을 알 수 없었다. 목록에서 빼면 `unsupported` 로 세어 보고된다.
    [[nodiscard]] bool IsSupportedClipExtension(const std::filesystem::path& path);

    // ★ 없는 폴더는 **오류가 아니라 빈 결과**다. 옛 배선은 순회 전체를
    //   `catch (...) {}` 로 감싸 폴더 부재·권한 오류·적재 실패가 전부 같은
    //   침묵으로 떨어졌다. 여기서는 세어서 돌려준다 — 부르는 쪽이 0 을 보고
    //   판단한다.
    [[nodiscard]] ClipScanReport LoadClipsFromDirectory(AudioService& service,
        const std::filesystem::path& directory);
}
