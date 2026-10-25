import std;
import Creator.Core.Base64;
import Creator.Core.ContentAbi;

namespace
{
    unsigned checks{};
    void require(bool condition, std::string_view label)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string(label));
        }
    }

    void reject(std::string_view input)
    {
        std::vector<std::uint8_t> output{1, 2, 3};
        require(!ce::base64::decode(input, output) && output.empty(), "invalid input must clear output");
    }
}

int main()
{
    try
    {
        static_assert(std::same_as<std::remove_cv_t<decltype(ce::content_abi::version)>, std::uint32_t>);
        require(ce::content_abi::version == 1u, "content ABI version");
        require(std::string_view{ce::content_abi::token} == "creator-content-v1", "content ABI token");
        const std::pair<std::string_view, std::string_view> vectors[]{
            {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
            {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
        for (const auto& [plain, encoded] : vectors)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(plain.data());
            require(ce::base64::encode(bytes, plain.size()) == encoded, "known encoding vector");
            std::vector<std::uint8_t> output;
            require(ce::base64::decode(encoded, output) && output == std::vector<std::uint8_t>(bytes, bytes + plain.size()), "known decoding vector");
        }
        require(ce::base64::encode(nullptr, 0).empty(), "empty null input");
        require(ce::base64::encode(nullptr, 1).empty(), "nonempty null input");
        const std::array<std::uint8_t, 6> binary{0, 1, 2, 253, 254, 255};
        require(ce::base64::encode(binary.data(), binary.size()) == "AAEC/f7/", "binary encoding vector");
        for (const auto invalid : {"A", "AA", "AAA", "!!!!", "=m9v", "Zg=A", "Zm=v", "Zh==", "Zm9=", "A===", "====", "Zg==AAAA", "Zm8=AAAA", "Zm9v!!!!", "Zm9vZh==", "Zm9vZm9=", "Zg==\r\n", "Zm 8", "Zg== "})
        {
            reject(invalid);
        }
        reject(std::string_view{"\0AAA", 4});
        const std::array<char, 4> non_ascii{static_cast<char>(0xff), 'A', 'A', 'A'};
        reject(std::string_view{non_ascii.data(), non_ascii.size()});
        for (std::size_t size = 0; size <= 512; ++size)
        {
            std::vector<std::uint8_t> input(size);
            for (std::size_t i = 0; i < size; ++i)
            {
                input[i] = static_cast<std::uint8_t>((i * 17 + size) & 0xffu);
            }
            const auto encoded = ce::base64::encode(input.data(), input.size());
            require(encoded.size() == ((size + 2) / 3) * 4, "encoded length");
            std::vector<std::uint8_t> output{9, 8, 7};
            require(ce::base64::decode(encoded, output), "binary decode");
            require(output == input, "binary round trip");
        }
        std::cout << "ENCODING_MODULES_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
