// Source-only regression fixture. UNEXECUTED: the requested migration explicitly
// forbids builds/tests/runs. Compile separately with ProfilerRenderingDiagnostics.cpp
// when execution is authorized; this is intentionally outside product projects.
#include "../../Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.h"

#include <bit>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace rendering_diagnostics_tests
{
    using namespace ce::profiler_viewer::diagnostics;

    void check(bool condition, const char* name, unsigned& failures)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    }

    template <typename T>
    void overwrite(std::vector<std::byte>& bytes, std::size_t offset, T value)
    {
        for (std::size_t index = 0; index < sizeof(T); ++index)
        {
            bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 0xff);
        }
    }

    void reject(const std::vector<std::byte>& bytes, const char* name, unsigned& failures)
    {
        rendering_snapshot unchanged;
        unchanged.width = 12345;
        unchanged.lastError = "preserved";
        check(!decode_rendering(bytes, unchanged), name, failures);
        check(unchanged.width == 12345 && unchanged.lastError == "preserved",
            "failed decode leaves output unchanged", failures);
    }

    int run()
    {
        unsigned failures{};
        rendering_snapshot original;
        original.backend = rendering_backend::vulkan;
        original.enabled = true;
        original.pipelineReady = true;
        original.width = 1920;
        original.height = 1080;
        original.framesRendered = 0x0102030405060708ull;
        original.views[0] = { 42, 73, 1920, 1080, true };
        original.views[1] = { 43, 74, 1280, 720, true };
        original.views[2] = { 44, 75, 512, 512, false };
        original.shadow[0].valid = true;
        original.shadow[0].hasDirectionalLight = true;
        original.shadow[0].lightDirection = { 0.25f, -1.0f, 0.5f };
        original.shadow[0].cascades[2] = { 80.f, 40.f, .01f, 50.f, .001f, 7 };
        original.cpuMs = 4.5;
        original.gpuMs = 2.25;
        original.lastGpuViewId = 42;
        original.lastGpuFrameId = 73;
        original.lastGpuSubmissionId = 101;
        original.gpuTemporalProvenance = { 1, 2, 1, 0, 73, 42, 5, 0, 1280, 720, 1920, 1080, false };
        original.temporalProvenance = original.gpuTemporalProvenance;
        original.gpuTemporalProvenance.publicationFrameId = 73;
        original.temporalProvenance.publicationFrameId = 73;
        original.gpuTemporalProvenance.spatialProvenanceAvailable = true;
        original.gpuTemporalProvenance.spatialMode = 2;
        original.gpuTemporalProvenance.deepDvcApplied = true;
        original.temporalProvenance = original.gpuTemporalProvenance;
        original.passTimings = { { "Shadow", 1.5 }, { "GBuffer", .75 } };
        original.validationMessages = { "first observed warning", "second warning" };
        original.lastError = "example renderer error";
        const auto encoded = encode_rendering(original);
        rendering_snapshot decoded;
        check(!encoded.empty() && encoded.size() <= maximum_rendering_bytes,
            "valid rendering payload bounded", failures);
        if (encoded.empty())
        {
            return 1;
        }
        check(decode_rendering(encoded, decoded), "round trip accepted", failures);
        check(encode_rendering(decoded) == encoded, "round trip preserves every serialized field", failures);
        check(decoded.gpuTemporalProvenance.renderWidth == 1280 &&
            decoded.gpuTemporalProvenance.displayWidth == 1920 &&
            decoded.gpuTemporalProvenance.realFrameId == 73 &&
            decoded.gpuTemporalProvenance.spatialProvenanceAvailable &&
            decoded.gpuTemporalProvenance.spatialMode == 2 && decoded.gpuTemporalProvenance.deepDvcApplied,
            "GPU sample retains exact resolution and real-frame identity", failures);
        auto spatialFrame = original;
        spatialFrame.gpuTemporalProvenance.resolutionState = 4;
        spatialFrame.gpuTemporalProvenance.upscaler = 0;
        spatialFrame.gpuTemporalProvenance.spatialMode = 1;
        check(decode_rendering(encode_rendering(spatialFrame), decoded) &&
            decoded.gpuTemporalProvenance.resolutionState == 4 && decoded.gpuTemporalProvenance.spatialMode == 1,
            "NIS scaling resolution survives v3 round trip", failures);
        spatialFrame.gpuTemporalProvenance.spatialMode = 2;
        check(encode_rendering(spatialFrame).empty(), "spatial scaling cannot be labeled sharpen-only", failures);
        spatialFrame = original;
        spatialFrame.gpuTemporalProvenance.spatialProvenanceAvailable = false;
        check(encode_rendering(spatialFrame).empty(), "unknown spatial provenance cannot claim applied effects", failures);
        spatialFrame = original;
        spatialFrame.gpuTemporalProvenance.spatialMode = 3;
        check(encode_rendering(spatialFrame).empty(), "unknown spatial mode rejected", failures);

        // With the three prefix strings empty, the explicit wire fields before
        // provenance occupy 614 bytes. A v2 provenance record is 57 bytes; v3
        // appends availability, mode and applied-DVC (one byte each).
        constexpr std::size_t provenanceOffset = 614;
        constexpr std::size_t oldProvenanceBytes = 57;
        constexpr std::size_t provenanceBytes = oldProvenanceBytes + 3;
        auto legacyV2 = encoded;
        legacyV2.erase(legacyV2.begin() + provenanceOffset + provenanceBytes + oldProvenanceBytes,
            legacyV2.begin() + provenanceOffset + 2 * provenanceBytes);
        legacyV2.erase(legacyV2.begin() + provenanceOffset + oldProvenanceBytes,
            legacyV2.begin() + provenanceOffset + provenanceBytes);
        overwrite(legacyV2, 4, std::uint32_t{2});
        check(decode_rendering(legacyV2, decoded) && decoded.gpuTemporalProvenance.realFrameId == 73 &&
            !decoded.gpuTemporalProvenance.spatialProvenanceAvailable &&
            !decoded.temporalProvenance.spatialProvenanceAvailable,
            "legacy v2 keeps temporal identity but spatial effects unknown", failures);
        rendering_snapshot legacyRoundTrip;
        check(decode_rendering(encode_rendering(decoded), legacyRoundTrip) &&
            !legacyRoundTrip.gpuTemporalProvenance.spatialProvenanceAvailable,
            "re-encoding legacy provenance does not fabricate spatial exclusion", failures);
        auto legacyV1 = encoded;
        legacyV1.erase(legacyV1.begin() + provenanceOffset, legacyV1.begin() + provenanceOffset + 2 * provenanceBytes);
        overwrite(legacyV1, 4, std::uint32_t{1});
        check(decode_rendering(legacyV1, decoded) && decoded.gpuTemporalProvenance.frameKind == 0 &&
            !decoded.gpuTemporalProvenance.spatialProvenanceAvailable && decoded.passTimings.size() == 2,
            "legacy v1 keeps timings without inventing frame/effect provenance", failures);
        check(decode_rendering(encoded, decoded), "restore current payload after legacy checks", failures);
        auto mixedFrame = original;
        mixedFrame.gpuTemporalProvenance.generatedOrdinal = 1;
        check(encode_rendering(mixedFrame).empty(), "generated ordinal cannot be counted as real", failures);
        auto missingResolution = original;
        missingResolution.gpuTemporalProvenance.renderWidth = 0;
        check(encode_rendering(missingResolution).empty(), "missing temporal resolution rejected", failures);
        check(decoded.views[0].viewId != decoded.views[1].viewId &&
            decoded.lastGpuViewId == decoded.views[0].viewId,
            "GPU sample retains selected-view identity", failures);

        reject({}, "empty payload", failures);
        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            reject(std::vector<std::byte>(encoded.begin(), encoded.begin() + size),
                "every truncated prefix rejected", failures);
        }
        auto malformed = encoded;
        malformed.push_back(std::byte{ 0 });
        reject(malformed, "trailing bytes", failures);
        malformed = encoded;
        malformed[0] ^= std::byte{ 1 };
        reject(malformed, "bad magic", failures);
        malformed = encoded;
        overwrite(malformed, 4, rendering_schema_version + 1);
        reject(malformed, "unsupported schema", failures);
        malformed = encoded;
        overwrite(malformed, 4, std::uint32_t{0});
        reject(malformed, "zero schema", failures);
        malformed = encoded;
        overwrite(malformed, 4, std::uint32_t{2});
        reject(malformed, "v3 bytes cannot be relabeled v2", failures);
        malformed = encoded;
        malformed[provenanceOffset + oldProvenanceBytes] = std::byte{2};
        reject(malformed, "noncanonical spatial availability", failures);
        malformed = encoded;
        malformed[provenanceOffset + oldProvenanceBytes + 2] = std::byte{2};
        reject(malformed, "noncanonical applied DVC flag", failures);
        malformed = encoded;
        malformed[8] = std::byte{ 2 }; // v1 backend field
        reject(malformed, "unknown backend", failures);
        malformed = encoded;
        malformed[9] = std::byte{ 2 }; // v1 enabled field
        reject(malformed, "noncanonical boolean", failures);
        reject(std::vector<std::byte>(maximum_rendering_bytes + 1), "oversized payload", failures);

        rendering_snapshot empty;
        const auto emptyBytes = encode_rendering(empty);
        // v1 suffix: pass count, validation count, two omitted counters, text flag.
        constexpr std::size_t trailer = 8 + 8 + 1;
        if (emptyBytes.size() < trailer + 8)
        {
            check(false, "empty snapshot encoded with v1 suffix", failures);
            return 1;
        }
        malformed = emptyBytes;
        overwrite(malformed, malformed.size() - trailer - 8,
            static_cast<std::uint32_t>(maximum_rendering_passes + 1));
        reject(malformed, "oversized pass count", failures);
        malformed = emptyBytes;
        overwrite(malformed, malformed.size() - trailer - 4,
            static_cast<std::uint32_t>(maximum_rendering_messages + 1));
        reject(malformed, "oversized validation count", failures);

        rendering_snapshot onePass;
        onePass.passTimings = { { "p", 1.0 } };
        malformed = encode_rendering(onePass);
        if (malformed.size() < trailer + 4 + sizeof(double) + 1 + 4)
        {
            check(false, "one-pass snapshot encoded with v1 suffix", failures);
            return 1;
        }
        // One pass finishes immediately before the empty validation count and trailer.
        const auto timingOffset = malformed.size() - trailer - 4 - sizeof(double);
        overwrite(malformed, timingOffset, std::bit_cast<std::uint64_t>(
            std::numeric_limits<double>::quiet_NaN()));
        reject(malformed, "nonfinite GPU timing", failures);
        malformed = encode_rendering(onePass);
        overwrite(malformed, timingOffset, std::bit_cast<std::uint64_t>(-1.0));
        reject(malformed, "negative GPU timing", failures);
        malformed = encode_rendering(onePass);
        overwrite(malformed, timingOffset - 1 - 4,
            static_cast<std::uint32_t>(maximum_rendering_name_bytes + 1));
        reject(malformed, "oversized pass name", failures);
        malformed = encode_rendering(onePass);
        malformed[timingOffset - 1] = std::byte{ 0 };
        reject(malformed, "embedded null text", failures);

        rendering_snapshot maximum;
        maximum.passTimings.assign(maximum_rendering_passes,
            { std::string(maximum_rendering_name_bytes, 'P'), 1.0 });
        maximum.validationMessages.assign(maximum_rendering_messages,
            std::string(maximum_rendering_message_bytes, 'M'));
        maximum.meshletFallback.assign(maximum_rendering_text_bytes, 'A');
        maximum.occlusionFallback.assign(maximum_rendering_text_bytes, 'B');
        maximum.skinningFallback.assign(maximum_rendering_text_bytes, 'C');
        maximum.lastGpuCollectError.assign(maximum_rendering_text_bytes, 'D');
        maximum.lastError.assign(maximum_rendering_text_bytes, 'E');
        maximum.omittedPassTimings = 9;
        maximum.omittedValidationMessages = 10;
        maximum.textTruncated = true;
        const auto maximumBytes = encode_rendering(maximum);
        check(!maximumBytes.empty() && maximumBytes.size() <= maximum_rendering_bytes,
            "all independent upper bounds fit total budget", failures);
        check(decode_rendering(maximumBytes, decoded) && decoded.omittedPassTimings == 9 &&
            decoded.omittedValidationMessages == 10 && decoded.textTruncated,
            "bounds and clipping metadata round trip", failures);
        maximum.passTimings.push_back({ "one too many", 1.0 });
        check(encode_rendering(maximum).empty(), "encoder rejects too many passes", failures);
        maximum.passTimings.pop_back();
        maximum.validationMessages.push_back("one too many");
        check(encode_rendering(maximum).empty(), "encoder rejects too many messages", failures);
        maximum.validationMessages.pop_back();
        maximum.lastError.push_back('x');
        check(encode_rendering(maximum).empty(), "encoder rejects oversized text", failures);
        onePass.passTimings[0].milliseconds = std::numeric_limits<double>::infinity();
        check(encode_rendering(onePass).empty(), "encoder rejects nonfinite timing", failures);
        return failures == 0 ? 0 : 1;
    }
}

int main()
{
    return rendering_diagnostics_tests::run();
}
