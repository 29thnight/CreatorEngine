#include "AudioDecodeValidation.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    int pass = 0;
    int fail = 0;
    for (const char* name : { "silence.wav", "sine.wav", "impulse.wav", "loop.wav", "silent.mp3", "silent.flac",
        "loop.mp3", "loop.flac", "corrupt.wav", "truncated.wav", "oversized.wav", "corrupt.mp3", "truncated.mp3",
        "oversized.mp3", "corrupt.flac", "truncated.flac", "oversized.flac", "tail.wav", "tail.mp3", "tail.flac" })
    {
        const std::string input(name);
        std::string error;
        audio_cook::DecodedSource decoded{};
        const bool expected = input.starts_with("silence") || input.starts_with("sine") || input.starts_with("impulse")
            || input.starts_with("loop") || input.starts_with("silent");
        const bool accepted = audio_cook::ValidateEncodedSource(std::filesystem::path(argv[1]) / input, decoded, error);
        const bool ok = accepted == expected && (!accepted
            || (decoded.channels == 1u && decoded.sampleRate > 0u && decoded.frameCount > 0u));
        std::printf("[%s] %s expected=%d accepted=%d rate=%u channels=%u frames=%llu reason=%s\n",
            ok ? "PASS" : "FAIL", name, expected, accepted, decoded.sampleRate, decoded.channels,
            static_cast<unsigned long long>(decoded.frameCount), error.c_str());
        if (ok)
        {
            ++pass;
        }
        else
        {
            ++fail;
        }
    }
    std::printf("SUMMARY checks=%d passed=%d failed=%d\n", pass + fail, pass, fail);
    return fail ? 1 : 0;
}
