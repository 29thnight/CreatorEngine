#pragma once

#include "../../FrameCameraSnapshot.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

// Diagnostic camera/clock slice only. Geometry, materials, pose, tuning and
// history are NOT archived here. No process-local pointer or publication ID is
// serialized. The consumer keeps its current frame/epoch/retirement identities.
struct EnhancedCameraReplayInput
{
    uint32_t width{}, height{}, target{}, viewFlags{};
    bool skyBoxEnabled{};
    float totalSeconds{}, deltaSeconds{};
    FrameCameraSnapshot camera{};

    static constexpr size_t kSize = 368;
    static constexpr std::array<uint8_t, 8> kMagic{'C','E','C','A','M','0','0','1'};

    static uint64_t Checksum(std::span<const uint8_t> bytes)
    {
        uint64_t hash = 14695981039346656037ull;
        for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
        return hash;
    }

    std::vector<uint8_t> Encode() const
    {
        std::vector<uint8_t> bytes(kMagic.begin(), kMagic.end());
        const auto word = [&](uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<uint8_t>(value >> shift));
        };
        const auto scalar = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
        const auto matrix = [&](const math::matrix4x4& value) {
            std::array<float, 16> values;
            static_assert(sizeof(value) == sizeof(values));
            std::memcpy(values.data(), &value, sizeof(values));
            for (float v : values) scalar(v);
        };
        const auto vector = [&](const math::vector3& v) { scalar(v.x); scalar(v.y); scalar(v.z); };
        word(1); word(width); word(height); word(target); word(viewFlags);
        word(skyBoxEnabled ? 1 : 0); scalar(totalSeconds); scalar(deltaSeconds);
        word(camera.isOrthographic ? 1 : 0);
        matrix(camera.view); matrix(camera.projection);
        matrix(camera.inverseView); matrix(camera.inverseProjection);
        vector(camera.eyePosition); vector(camera.forward); vector(camera.right); vector(camera.up);
        scalar(camera.fov); scalar(camera.nearPlane); scalar(camera.farPlane);
        const auto hash = Checksum(bytes);
        for (unsigned shift = 0; shift < 64; shift += 8)
            bytes.push_back(static_cast<uint8_t>(hash >> shift));
        return bytes;
    }

    static bool Decode(std::span<const uint8_t> bytes, EnhancedCameraReplayInput& output,
        std::string& error)
    {
        const auto reject = [&](const char* why) { error = why; return false; };
        if (bytes.size() != kSize || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
            return reject("camera replay size or magic mismatch");
        uint64_t savedHash{};
        for (unsigned i = 0; i < 8; ++i) savedHash |= uint64_t(bytes[kSize - 8 + i]) << (8 * i);
        if (savedHash != Checksum(bytes.first(kSize - 8))) return reject("camera replay checksum mismatch");
        size_t offset = kMagic.size();
        const auto word = [&]() {
            uint32_t value{};
            for (unsigned i = 0; i < 4; ++i) value |= uint32_t(bytes[offset++]) << (8 * i);
            return value;
        };
        bool finite = true;
        const auto scalar = [&]() {
            const float value = std::bit_cast<float>(word());
            finite &= std::isfinite(value);
            return value;
        };
        const auto matrix = [&](math::matrix4x4& value) {
            std::array<float, 16> values;
            for (float& v : values) v = scalar();
            std::memcpy(&value, values.data(), sizeof(values));
        };
        const auto vector = [&](math::vector3& v) { v.x = scalar(); v.y = scalar(); v.z = scalar(); };
        EnhancedCameraReplayInput candidate;
        if (word() != 1) return reject("unsupported camera replay version");
        candidate.width = word(); candidate.height = word(); candidate.target = word(); candidate.viewFlags = word();
        const auto sky = word(); candidate.skyBoxEnabled = sky == 1;
        candidate.totalSeconds = scalar(); candidate.deltaSeconds = scalar();
        const auto ortho = word(); candidate.camera.isOrthographic = ortho == 1;
        matrix(candidate.camera.view); matrix(candidate.camera.projection);
        matrix(candidate.camera.inverseView); matrix(candidate.camera.inverseProjection);
        vector(candidate.camera.eyePosition); vector(candidate.camera.forward);
        vector(candidate.camera.right); vector(candidate.camera.up);
        candidate.camera.fov = scalar(); candidate.camera.nearPlane = scalar(); candidate.camera.farPlane = scalar();
        if (!finite || !candidate.width || !candidate.height || candidate.width > 16384 || candidate.height > 16384
            || candidate.target > 1 || (candidate.viewFlags & ~15u) || sky > 1 || ortho > 1
            || candidate.totalSeconds < 0 || candidate.deltaSeconds < 0
            || candidate.camera.nearPlane <= 0 || candidate.camera.farPlane <= candidate.camera.nearPlane
            || (!candidate.camera.isOrthographic && (candidate.camera.fov <= 0 || candidate.camera.fov >= 180)))
            return reject("invalid camera replay values");
        output = candidate; // Rejection never partially changes the accepted input.
        error.clear();
        return true;
    }

    static bool Load(const std::filesystem::path& path, EnhancedCameraReplayInput& output, std::string& error)
    {
        std::error_code ec;
        if (!path.is_absolute() || std::filesystem::file_size(path, ec) != kSize || ec)
        { error = "camera replay requires an absolute file of the exact supported size"; return false; }
        std::array<uint8_t, kSize> bytes{};
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (!stream || stream.peek() != std::char_traits<char>::eof())
        { error = "camera replay read failed or size changed"; return false; }
        return Decode(bytes, output, error);
    }
};
