#pragma once
#include "RHI/RHIShaderBlob.h"
#include <reflgen/runtime/value_descriptor.h>

namespace reflgen
{
    // RHIShaderBlob owns an exact byte range. Its raw Data() pointer remains
    // opaque, while this readonly indexed view exposes only [0, Size()).
    template<> struct runtime_value_traits<RHIShaderBlob>
    {
        static constexpr value_shape shape = value_shape::sequence;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<std::uint8_t>;
        static std::size_t size(const RHIShaderBlob& value) noexcept { return value.Size(); }
        static const std::uint8_t& at(const RHIShaderBlob& value, std::size_t index)
        {
            if (index >= value.Size()) throw std::out_of_range("shader byte index is outside its owned buffer");
            return static_cast<const std::uint8_t*>(value.Data())[index];
        }
    };
}
