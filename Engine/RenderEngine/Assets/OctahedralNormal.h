#pragma once
#include "QuantizedGeometry.h"

namespace assets::cluster
{
    inline std::array<float, 3> DecodeOctahedralNormal(uint32_t code)
    {
        quantized::Require(code < 16777216u && (code & 4095u) != 2048u && ((code >> 12) & 4095u) != 2048u,
                           "Octahedral signed code");
        const auto signedCode = [](uint32_t value) { return int32_t(value & 4095u) - ((value & 2048u) ? 4096 : 0); };
        float x = float(signedCode(code)) / 2047.f;
        float y = float(signedCode(code >> 12)) / 2047.f;
        const float z = 1.f - std::abs(x) - std::abs(y);
        const float fold = (std::max)(-z, 0.f);
        x += x >= 0 ? -fold : fold;
        y += y >= 0 ? -fold : fold;
        const float inverse = 1.f / std::sqrt(x * x + y * y + z * z);
        return {x * inverse, y * inverse, z * inverse};
    }
    inline uint32_t EncodeOctahedralNormal(const std::array<float, 3>& normal)
    {
        const double length = std::abs(double(normal[0])) + std::abs(double(normal[1])) + std::abs(double(normal[2]));
        quantized::Require(std::isfinite(length) && length > 0, "Octahedral normal direction");
        double x = normal[0] / length, y = normal[1] / length;
        if (normal[2] < 0)
        {
            const auto oldX = x;
            x = (1 - std::abs(y)) * (oldX >= 0 ? 1 : -1);
            y = (1 - std::abs(oldX)) * (y >= 0 ? 1 : -1);
        }
        const auto quantize = [](double value)
        { return uint32_t(int32_t(std::clamp(std::round(value * 2047), -2047., 2047.))) & 4095u; };
        return quantize(x) | (quantize(y) << 12);
    }
} // namespace assets::cluster
