#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Sha256.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#include "../../../Engine/Contracts/Compiler/Creator.RotateIntrinsic.h"

#include "../../../Engine/Containers/Creator.Containers.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace ce::crypto
{
    using sha256_digest = ce::fixed_array<std::uint8_t, 32>;

    class sha256
    {
      public:
        void update(const void* data, std::size_t length) noexcept
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            bit_count_ += static_cast<std::uint64_t>(length) * 8u;

            while (length > 0)
            {
                const std::size_t room = block_size_ - buffer_used_;
                const std::size_t take = (length < room) ? length : room;
                std::memcpy(buffer_ + buffer_used_, p, take);
                buffer_used_ += take;
                p += take;
                length -= take;

                if (buffer_used_ == block_size_)
                {
                    transform(buffer_);
                    buffer_used_ = 0;
                }
            }
        }

        // 32바이트 다이제스트를 낸다. 호출한 뒤 이 객체는 다시 쓰지 않는다.
        [[nodiscard]] sha256_digest finish() noexcept
        {
            const std::uint64_t bits = bit_count_;

            const std::uint8_t pad = 0x80;
            update(&pad, 1);
            while (buffer_used_ != 56)
            {
                const std::uint8_t zero = 0;
                update(&zero, 1);
            }
            // 길이는 빅엔디언 64비트. Update를 거치면 m_bitCount가 또 늘므로
            // 버퍼에 직접 적는다.
            for (int i = 0; i < 8; ++i)
            {
                buffer_[56 + i] = static_cast<std::uint8_t>((bits >> (56 - i * 8)) & 0xFFu);
            }
            transform(buffer_);

            sha256_digest out{};
            for (int i = 0; i < 8; ++i)
            {
                out[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
                out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
                out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
                out[i * 4 + 3] = static_cast<std::uint8_t>((state_[i]) & 0xFFu);
            }
            return out;
        }

        [[nodiscard]] static sha256_digest compute(const void* data, std::size_t length) noexcept
        {
            sha256 sha;
            sha.update(data, length);
            return sha.finish();
        }

      private:
        static constexpr std::size_t block_size_ = 64;

        static std::uint32_t rotate_right(std::uint32_t v, int n) noexcept { return (v >> n) | (v << (32 - n)); }
#if defined(_MSC_VER)
        // _rotr 는 /Od 에서도 회전 명령 하나다. 함수 호출로 남는 rotate_right 가
        // Debug 텍스처 검증 해시 시간의 절반을 차지했다.
#define CE_SHA256_ROTR(v, n) _rotr((v), (n))
#else
#define CE_SHA256_ROTR(v, n) rotate_right((v), (n))
#endif

        void transform(const std::uint8_t block[block_size_]) noexcept
        {
            // FIPS 180-4 §4.2.2 — 처음 64개 소수의 세제곱근 소수부 앞 32비트.
            static constexpr std::uint32_t round_constants[64] = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
            };

            std::uint32_t w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                       (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                       (static_cast<std::uint32_t>(block[i * 4 + 3]));
            }
            for (int i = 16; i < 64; ++i)
            {
                const std::uint32_t s0 =
                    CE_SHA256_ROTR(w[i - 15], 7) ^ CE_SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const std::uint32_t s1 = CE_SHA256_ROTR(w[i - 2], 17) ^ CE_SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
            std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
            for (int i = 0; i < 64; ++i)
            {
                const std::uint32_t sigma1 = CE_SHA256_ROTR(e, 6) ^ CE_SHA256_ROTR(e, 11) ^ CE_SHA256_ROTR(e, 25);
                const std::uint32_t ch = (e & f) ^ (~e & g);
                const std::uint32_t temp1 = h + sigma1 + ch + round_constants[i] + w[i];
                const std::uint32_t sigma0 = CE_SHA256_ROTR(a, 2) ^ CE_SHA256_ROTR(a, 13) ^ CE_SHA256_ROTR(a, 22);
                const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temp2 = sigma0 + maj;

                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state_[0] += a;
            state_[1] += b;
            state_[2] += c;
            state_[3] += d;
            state_[4] += e;
            state_[5] += f;
            state_[6] += g;
            state_[7] += h;
        }

        // FIPS 180-4 §5.3.3 — 처음 8개 소수의 제곱근 소수부 앞 32비트.
        std::uint32_t state_[8]{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::uint8_t buffer_[block_size_]{};
        std::size_t buffer_used_{0};
        std::uint64_t bit_count_{0};
    };
#undef CE_SHA256_ROTR

    // 소문자 16진 64자.
    inline std::string to_hex(const sha256_digest& digest)
    {
        static constexpr char hex_digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(64);
        for (std::uint8_t b : digest)
        {
            s.push_back(hex_digits[b >> 4]);
            s.push_back(hex_digits[b & 0x0Fu]);
        }
        return s;
    }
} // namespace ce::crypto
