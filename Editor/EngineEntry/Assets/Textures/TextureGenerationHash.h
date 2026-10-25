#pragma once

#include "../../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

#pragma comment(lib, "bcrypt.lib")

// Large editor cache validation streams use the same SHA-256 bytes as the
// identity implementation, without running its scalar rounds in Debug builds.
// Provider failures reject the generation; they never bypass verification.
class TextureGenerationHash final
{
public:
    TextureGenerationHash()
    {
        m_status = BCryptOpenAlgorithmProvider(&m_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (m_status < 0) return;
        DWORD bytes{}, returned{};
        m_status = BCryptGetProperty(m_algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &returned, 0);
        if (m_status < 0) return;
        if (bytes == 0 || returned != sizeof(bytes)) { m_status = -1; return; }
        m_object.resize(bytes);
        m_status = BCryptCreateHash(m_algorithm, &m_hash, m_object.data(), bytes, nullptr, 0, 0);
    }
    ~TextureGenerationHash()
    {
        if (m_hash) BCryptDestroyHash(m_hash);
        if (m_algorithm) BCryptCloseAlgorithmProvider(m_algorithm, 0);
    }
    TextureGenerationHash(const TextureGenerationHash&) = delete;
    TextureGenerationHash& operator=(const TextureGenerationHash&) = delete;

    void Update(const void* data, std::size_t size)
    {
        const auto* cursor = static_cast<const unsigned char*>(data);
        while (m_status >= 0 && size)
        {
            const auto count = static_cast<ULONG>((std::min)(size,
                static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
            m_status = BCryptHashData(m_hash, const_cast<PUCHAR>(cursor), count, 0);
            cursor += count;
            size -= count;
        }
    }
    bool Finish(std::array<std::uint8_t, 32>& output, std::string& failure)
    {
        std::array<std::uint8_t, 32> digest{};
        if (m_status >= 0)
            m_status = BCryptFinishHash(m_hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        if (m_status < 0)
        {
            failure = "Texture generation SHA-256 provider failed";
            return false;
        }
        output = digest;
        failure.clear();
        return true;
    }
private:
    BCRYPT_ALG_HANDLE m_algorithm{};
    BCRYPT_HASH_HANDLE m_hash{};
    std::vector<unsigned char> m_object;
    NTSTATUS m_status{};
};
