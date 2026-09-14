#include "../VulkanSelfTest.h"

#include <AuthoringRymlErrorPolicy.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/yml/emit.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

// PBR-W9 — 두 제품 캡처의 픽셀을 실제로 맞대고 판정한다.
//
// ★ W0부터 캡처는 float32 원본 7장을 남겼지만 **그 바이트를 읽는 코드가
//   저장소에 하나도 없었다.** 게이트는 파일 크기만 재고 있었고, 계획서에 적힌
//   RMSE 수치는 손으로 한 번 잰 값이라 회귀를 잡을 수 없었다. 생산만 있고
//   소비가 0인 파이프라인이었다.
//
// 이 비교기는 두 캡처(보통 DX12와 Vulkan)의 같은 이름 attachment를 열어
// 최대 절대 편차·RMSE·허용치를 넘은 픽셀 수를 낸다. 허용식은 기존 PBR parity와
// 같다 — 새 자를 만들면 두 검사가 다른 말을 하게 된다.
namespace
{
    struct AttachmentInfo
    {
        std::string name;
        std::string file;
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t channels{};
    };

    struct CompareResult
    {
        std::string name;
        double maxDelta{ 0.0 };
        double rmse{ 0.0 };
        std::uint64_t exceeded{ 0 };
        std::uint64_t samples{ 0 };
        bool gated{ true };
    };

    bool ReadText(const std::filesystem::path& path, std::string& out, std::string& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "파일을 열 수 없다: " + path.string(); return false; }
        out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    bool ReadManifest(const std::filesystem::path& directory,
        std::vector<AttachmentInfo>& out, std::string& backend, std::string& error)
    {
        std::string text;
        if (!ReadText(directory / "manifest.json", text, error)) return false;
        Authoring::EnsureRymlErrorPolicy();
        const ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(text));
        const ryml::ConstNodeRef root = tree.rootref();
        if (!root.is_map() || !root.has_child("attachments"))
        {
            error = "manifest에 attachments가 없다: " + directory.string();
            return false;
        }
        if (root.has_child("backend")) root["backend"] >> backend;
        for (const ryml::ConstNodeRef node : root["attachments"])
        {
            AttachmentInfo info;
            node["name"] >> info.name;
            node["file"] >> info.file;
            node["width"] >> info.width;
            node["height"] >> info.height;
            node["channels"] >> info.channels;
            out.push_back(std::move(info));
        }
        return !out.empty();
    }

    bool ReadFloats(const std::filesystem::path& path, std::uint64_t expected,
        std::vector<float>& out, std::string& error)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) { error = "readback을 열 수 없다: " + path.string(); return false; }
        const std::uint64_t bytes = static_cast<std::uint64_t>(input.tellg());
        if (bytes != expected * sizeof(float))
        {
            error = "readback 크기가 manifest와 다르다: " + path.string();
            return false;
        }
        out.resize(static_cast<std::size_t>(expected));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(bytes));
        if (!input) { error = "readback을 끝까지 읽지 못했다: " + path.string(); return false; }
        return true;
    }

    // ★ 어떤 attachment가 **판정 대상**인가.
    //
    // 실측(2026-09-14, FT_Primitives 1920x1080)이 경계를 그어 주었다.
    //   · GBuffer 다섯 장(baseColor·metalRough·normal·emissive·depth)은
    //     백엔드가 달라도, 시각이 5.7초 벌어져도 **초과 0**이었다.
    //   · preToneHdr·display 는 **같은 백엔드끼리도** 시각만 벌어지면 넘었다.
    //       dx12 vs dx12  Δt 0.27s → 19,490 초과 (max 0.043)
    //       dx12 vs dx12  Δt 3.11s → 57,977 초과 (max 0.039)
    //       dx12 vs vulkan Δt 5.68s → 241,936 초과 (max 0.119)
    //
    // 원인은 켜져 있는 시간 구동·시간축 누적 효과다 — 움직이는 구름 그림자
    // (shadow.cloudMoveSpeed), 볼류메트릭 포그의 직전 프레임 혼합
    // (mPreviousFrameBlendFactor), SSGI 누적. 그런데 캡처는 시뮬레이션 시각을
    // 고정할 수단이 없다(`time.*` 명령이 없다). 두 캡처는 **다른 시각의 서로
    // 다른 그림**이고, 그것을 픽셀로 맞대는 것은 애초에 성립하지 않는 질문이다.
    //
    // 그래서 허용치를 늘려 초록으로 만들지 않는다 — 그러면 이 두 장에 대해
    // 게이트가 아무것도 재지 않으면서 재는 척하게 된다. 판정에서 빼되 **수는
    // 매 실행 남긴다**(gated=false). 시각을 고정할 수 있게 되는 날 이 자리를
    // 판정으로 되돌리는 것이 W9의 남은 단위다.
    [[nodiscard]] bool IsGatedAttachment(const std::string& name)
    {
        return name != "preToneHdr" && name != "display";
    }

    // 기존 render.pbr.parity와 같은 허용식이다. 절대 0.002 + 상대 0.5%.
    bool WithinTolerance(float a, float b, double& outDelta)
    {
        outDelta = std::abs(static_cast<double>(a) - static_cast<double>(b));
        const double scale = std::max(std::abs(static_cast<double>(a)),
            std::abs(static_cast<double>(b)));
        return outDelta <= 0.002 + 0.005 * scale;
    }
}

bool RunPbrCaptureCompare(const std::string& leftDirectory,
    const std::string& rightDirectory, const std::string& outputPath,
    std::string& outLog)
{
    std::string error;
    std::vector<AttachmentInfo> left, right;
    std::string leftBackend, rightBackend;
    if (!ReadManifest(leftDirectory, left, leftBackend, error)
        || !ReadManifest(rightDirectory, right, rightBackend, error))
    {
        outLog += error + "\n";
        return false;
    }
    if (leftBackend == rightBackend)
    {
        // 같은 backend 둘을 맞대면 "둘 다 같은 이유로 틀린" 경우를 통과시킨다.
        // 대조군은 독립 유도를 가져야 한다.
        outLog += "두 캡처의 backend가 같다 (" + leftBackend + ") — 교차 판정이 아니다\n";
        return false;
    }

    bool passed = true;
    std::vector<CompareResult> results;
    for (const AttachmentInfo& info : left)
    {
        const auto found = std::find_if(right.begin(), right.end(),
            [&info](const AttachmentInfo& other) { return other.name == info.name; });
        if (found == right.end())
        {
            outLog += "오른쪽 캡처에 " + info.name + " attachment가 없다\n";
            passed = false;
            continue;
        }
        if (found->width != info.width || found->height != info.height
            || found->channels != info.channels)
        {
            outLog += info.name + " attachment의 크기/채널이 다르다\n";
            passed = false;
            continue;
        }

        const std::uint64_t samples = static_cast<std::uint64_t>(info.width)
            * info.height * info.channels;
        std::vector<float> a, b;
        if (!ReadFloats(std::filesystem::path(leftDirectory) / info.file, samples, a, error)
            || !ReadFloats(std::filesystem::path(rightDirectory) / found->file, samples, b, error))
        {
            outLog += error + "\n";
            passed = false;
            continue;
        }

        CompareResult result;
        result.name = info.name;
        result.samples = samples;
        result.gated = IsGatedAttachment(info.name);
        double squared = 0.0;
        for (std::uint64_t i = 0; i < samples; ++i)
        {
            const float x = a[static_cast<std::size_t>(i)];
            const float y = b[static_cast<std::size_t>(i)];
            if (!std::isfinite(x) || !std::isfinite(y))
            {
                // 비유한 값은 편차로 접을 수 없다. 그 자체가 실패다.
                ++result.exceeded;
                result.maxDelta = std::numeric_limits<double>::infinity();
                continue;
            }
            double delta = 0.0;
            if (!WithinTolerance(x, y, delta)) ++result.exceeded;
            result.maxDelta = std::max(result.maxDelta, delta);
            squared += delta * delta;
        }
        result.rmse = samples ? std::sqrt(squared / static_cast<double>(samples)) : 0.0;
        if (0 != result.exceeded && result.gated) passed = false;
        results.push_back(result);
    }

    // 결과는 늘 남긴다 — 통과해도 수를 남겨야 다음 실행이 회귀를 볼 수 있다.
    ryml::Tree tree;
    auto root = tree.rootref();
    root |= ryml::MAP;
    root["left"] << leftDirectory;
    root["right"] << rightDirectory;
    root["leftBackend"] << leftBackend;
    root["rightBackend"] << rightBackend;
    root["tolerance"] << "abs 0.002 + rel 0.5%";
    root["ungatedReason"] << "preToneHdr/display 은 시각 구동·시간축 누적 효과가 "
        "켜져 있고 캡처가 시뮬레이션 시각을 고정할 수 없어 측정만 한다";
    root["passed"] << passed;
    root["attachments"] |= ryml::SEQ;
    for (const CompareResult& result : results)
    {
        auto node = root["attachments"].append_child();
        node |= ryml::MAP;
        node["name"] << result.name;
        node["samples"] << result.samples;
        node["maxDelta"] << result.maxDelta;
        node["rmse"] << result.rmse;
        node["exceeded"] << result.exceeded;
        node["gated"] << result.gated;
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "  %-12s max %.6f rmse %.6f exceeded %llu/%llu%s\n",
            result.name.c_str(), result.maxDelta, result.rmse,
            static_cast<unsigned long long>(result.exceeded),
            static_cast<unsigned long long>(result.samples),
            result.gated ? "" : "  [측정만 · 시각 고정 불가]");
        outLog += line;
    }
    if (!outputPath.empty())
    {
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            outLog += "비교 결과를 쓸 수 없다: " + outputPath + "\n";
            return false;
        }
        output << ryml::emitrs_json<std::string>(tree) << '\n';
    }
    outLog += passed ? "PBR capture compare PASS\n" : "PBR capture compare FAIL\n";
    return passed;
}
