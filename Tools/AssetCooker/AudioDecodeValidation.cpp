#include "AudioDecodeValidation.h"

#include "Assets/AudioClipSourceMetadata.h"
#include "../../Engine/SceneRuntime/Audio/AudioSourceValidation.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

// AssetCooker is a separate executable. This is its sole miniaudio
// implementation TU; the runtime has its own private miniaudio implementation.
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push, 0)
#include "../../ThirdParty/miniaudio/miniaudio.h"
#pragma warning(pop)

namespace audio_cook
{

    bool ValidateEncodedSource(const std::filesystem::path& source,
        DecodedSource& out, std::string& failure)
    {
        assets::AudioClipSourceMetadata stamp{};
        if (!assets::InspectAudioClipSource(source, stamp, failure))
        {
            return false;
        }

        std::uint64_t declaredFrames = 0u;
        if (!wave::ValidateAudioSourceContainer(source, stamp, declaredFrames, failure))
        {
            return false;
        }

        ma_decoder decoder{};
        const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0u, 0u);
#if defined(_WIN32)
        const ma_result opened = ma_decoder_init_file_w(source.c_str(), &config, &decoder);
#else
        const ma_result opened = ma_decoder_init_file(source.c_str(), &config, &decoder);
#endif
        if (opened != MA_SUCCESS)
        {
            failure = std::string("audio decoder open failed: ") + ma_result_description(opened);
            return false;
        }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0u;
        ma_uint32 sampleRate = 0u;
        const ma_result formatResult = ma_decoder_get_data_format(&decoder,
            &format, &channels, &sampleRate, nullptr, 0u);
        if (formatResult != MA_SUCCESS || format != ma_format_f32
            || channels == 0u || channels > 2u || sampleRate == 0u)
        {
            ma_decoder_uninit(&decoder);
            failure = "audio decoder reported unsupported channels/sample rate";
            return false;
        }

        std::vector<float> pcm(4096u * channels);
        std::uint64_t decodedFrames = 0u;
        for (;;)
        {
            ma_uint64 framesRead = 0u;
            const ma_result read = ma_decoder_read_pcm_frames(&decoder,
                pcm.data(), 4096u, &framesRead);
            if (read != MA_SUCCESS && read != MA_AT_END)
            {
                ma_decoder_uninit(&decoder);
                failure = std::string("audio decoder read failed: ")
                    + ma_result_description(read);
                return false;
            }
            decodedFrames += framesRead;
            if (read == MA_AT_END)
            {
                break;
            }
            if (framesRead == 0u)
            {
                ma_decoder_uninit(&decoder);
                failure = "audio decoder made no progress before EOF";
                return false;
            }
        }
        ma_decoder_uninit(&decoder);
        if (decodedFrames == 0u || (declaredFrames != 0u && decodedFrames != declaredFrames))
        {
            failure = "decoded PCM frame count does not match container metadata";
            return false;
        }

        out = DecodedSource{ channels, sampleRate, decodedFrames };
        failure.clear();
        return true;
    }
}
