void RunOctahedralNormalContract()
{
    using namespace assets;
    using namespace assets::cluster;
    using assets::quantized::Put;
    const auto mask = kModelVertexMasks.front(), stride = StrideOf(mask);
    constexpr uint32_t count = 64;
    ce::dynamic_array<std::byte> raw(count * stride);
    double maxError{}, minDot = 1;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto z = 1.f - 2.f * (float(i) + .5f) / count;
        const auto radius = std::sqrt(1.f - z * z);
        const float normal[3]{radius * std::cos(i * 2.39996323f), radius * std::sin(i * 2.39996323f), z};
        std::memcpy(raw.data() + i * stride + 12, normal, 12);
    }
    auto grid = MakeGrid(mask, count, raw);
    const auto oct = Encode(mask, count, raw, grid);
    grid.octahedralNormals = false;
    auto scalar = Encode(mask, count, raw, grid);
    Check(quantized::Word(oct, 28) == 1 && oct.size() < scalar.size(), "Directional codec reduces actual packet bytes");
    ce::dynamic_array<std::byte> decoded;
    std::string error;
    Check(Decode(oct, decoded, error), "Octahedral CPU packet decode " + error);
    for (uint32_t i = 0; i < count; ++i)
    {
        double dot{};
        for (unsigned component = 0; component < 3; ++component)
        {
            const auto offset = i * stride + 12 + component * 4;
            const auto a = std::bit_cast<float>(quantized::Word(raw, offset));
            const auto b = std::bit_cast<float>(quantized::Word(decoded, offset));
            maxError = std::max(maxError, double(std::abs(a - b)));
            dot += double(a) * b;
        }
        minDot = std::min(minDot, dot);
    }
    Check(maxError <= Policy{}.absoluteError[1] && minDot > .999998, "Normal angular/component error bound");
    for (auto direction : {ce::fixed_array<float, 3>{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}})
    {
        const auto normal = DecodeOctahedralNormal(EncodeOctahedralNormal(direction));
        double dot{};
        for (unsigned c = 0; c < 3; ++c)
        {
            dot += double(normal[c]) * direction[c];
        }
        Check(dot > .999998, "Octahedral axes and fold seam");
    }
    auto corrupt = oct;
    Put(corrupt, 28, 2);
    Check(!Decode(corrupt, decoded, error), "Reject unknown normal codec");
    corrupt = oct;
    Put(corrupt, 36 + 3 * 8, std::bit_cast<uint32_t>(.0001f));
    Check(!Decode(corrupt, decoded, error), "Reject a quantized octahedral code channel");
    Put(scalar, 4, 1);
    Check(Decode(scalar, decoded, error), "Existing scalar direction packets remain readable");
    const float nonunit[3]{2, 0, 0};
    std::memcpy(raw.data() + 12, nonunit, 12);
    const auto fallback = Encode(mask, count, raw, MakeGrid(mask, count, raw));
    Check(quantized::Word(fallback, 28) == 0 && Decode(fallback, decoded, error) &&
              quantized::Word(decoded, 12) == std::bit_cast<uint32_t>(2.f),
          "Non-unit authored vectors retain the scalar representation");
    const auto mixedGrid = MakeGrid(mask, count, raw);
    const auto unitPage = Encode(mask, 1, std::span<const std::byte>(raw).subspan(stride, stride), mixedGrid);
    Check(quantized::Word(unitPage, 28) == 0, "A mixed mesh retains the same normal codec in every meshlet");
    std::cout << "METEOR_OCT_NORMAL_OK directions=70 octBytes=" << oct.size() << " scalarBytes=" << scalar.size()
              << " maxComponentError=" << maxError << " minDot=" << minDot << " malformed=2 fallback=1 validation=0\n";
}
