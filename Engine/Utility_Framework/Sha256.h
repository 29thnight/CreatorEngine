#pragma once
// SHA-256 (FIPS 180-4) 자체 구현 — 모델 자산 신원 프로필 `ce.uuidv8.sha256.v1`의 해시.
//
// ── 왜 BCrypt가 아닌가 ──
//   저장소에는 이미 BCrypt 기반 `experiment::cooked::ComputeSha256`(cooked artifact
//   fingerprint)이 있다. 신원 유도는 그것을 쓰지 않는다:
//     · 신원은 개발기·AssetCooker·Player staging 어디서나 **같은 바이트**를 내야
//       한다(ModelAssetBigBangCutoverPlan §2.4). OS 제공자 핸들을 여는 코드는
//       실패 경로(제공자 열기 실패)가 있고, 그 실패는 "신원을 못 만든다"가 아니라
//       조용한 fallback 유혹이 된다. 순수 함수는 실패 경로가 없다.
//     · 유도당 알고리즘 제공자를 열고 닫는 비용이 없다 — 한 모델의 subasset
//       수십 개를 한 transaction에서 유도한다.
//   대신 BCrypt 구현은 **독립 대조군**으로 쓴다: AssetIdentitySelfTest가 같은
//   입력을 둘에 넣어 다이제스트가 같음을 단정한다(FIPS 공표 벡터와 함께).
//
// ── 헤더 온리인 이유 ──
//   Uuid.h(Sha1)와 같다. TypeTrait.h가 Core.Minimal.h를 통해 거의 전 저장소에
//   퍼져 있어 .cpp를 더하면 링크 그래프와 vcxproj를 건드려야 한다.
//
// 상태는 h[8]과 64바이트 블록 버퍼뿐이다. Finish 뒤에는 다시 쓰지 않는다.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace Hash
{
    using Sha256Digest = std::array<std::uint8_t, 32>;

    class Sha256
    {
    public:
        void Update(const void* data, std::size_t length) noexcept
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            m_bitCount += static_cast<std::uint64_t>(length) * 8u;

            while (length > 0)
            {
                const std::size_t room = kBlockSize - m_bufferUsed;
                const std::size_t take = (length < room) ? length : room;
                std::memcpy(m_buffer + m_bufferUsed, p, take);
                m_bufferUsed += take;
                p += take;
                length -= take;

                if (m_bufferUsed == kBlockSize)
                {
                    Transform(m_buffer);
                    m_bufferUsed = 0;
                }
            }
        }

        // 32바이트 다이제스트를 낸다. 호출한 뒤 이 객체는 다시 쓰지 않는다.
        [[nodiscard]] Sha256Digest Finish() noexcept
        {
            const std::uint64_t bits = m_bitCount;

            const std::uint8_t pad = 0x80;
            Update(&pad, 1);
            while (m_bufferUsed != 56)
            {
                const std::uint8_t zero = 0;
                Update(&zero, 1);
            }
            // 길이는 빅엔디언 64비트. Update를 거치면 m_bitCount가 또 늘므로
            // 버퍼에 직접 적는다.
            for (int i = 0; i < 8; ++i)
            {
                m_buffer[56 + i] =
                    static_cast<std::uint8_t>((bits >> (56 - i * 8)) & 0xFFu);
            }
            Transform(m_buffer);

            Sha256Digest out{};
            for (int i = 0; i < 8; ++i)
            {
                out[i * 4 + 0] = static_cast<std::uint8_t>((m_h[i] >> 24) & 0xFFu);
                out[i * 4 + 1] = static_cast<std::uint8_t>((m_h[i] >> 16) & 0xFFu);
                out[i * 4 + 2] = static_cast<std::uint8_t>((m_h[i] >> 8) & 0xFFu);
                out[i * 4 + 3] = static_cast<std::uint8_t>((m_h[i]) & 0xFFu);
            }
            return out;
        }

        [[nodiscard]] static Sha256Digest Compute(
            const void* data, std::size_t length) noexcept
        {
            Sha256 sha;
            sha.Update(data, length);
            return sha.Finish();
        }

    private:
        static constexpr std::size_t kBlockSize = 64;

        static std::uint32_t Rotr(std::uint32_t v, int n) noexcept
        {
            return (v >> n) | (v << (32 - n));
        }

        void Transform(const std::uint8_t block[kBlockSize]) noexcept
        {
            // FIPS 180-4 §4.2.2 — 처음 64개 소수의 세제곱근 소수부 앞 32비트.
            static constexpr std::uint32_t kRound[64] = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
                0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
                0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
                0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
                0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
                0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
                0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
                0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
                0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
            };

            std::uint32_t w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24)
                     | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                     | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                     | (static_cast<std::uint32_t>(block[i * 4 + 3]));
            }
            for (int i = 16; i < 64; ++i)
            {
                const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18)
                    ^ (w[i - 15] >> 3);
                const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19)
                    ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
            std::uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
            for (int i = 0; i < 64; ++i)
            {
                const std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
                const std::uint32_t ch = (e & f) ^ (~e & g);
                const std::uint32_t temp1 = h + S1 + ch + kRound[i] + w[i];
                const std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
                const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t temp2 = S0 + maj;

                h = g; g = f; f = e; e = d + temp1;
                d = c; c = b; b = a; a = temp1 + temp2;
            }
            m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d;
            m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
        }

        // FIPS 180-4 §5.3.3 — 처음 8개 소수의 제곱근 소수부 앞 32비트.
        std::uint32_t m_h[8]{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
        std::uint8_t  m_buffer[kBlockSize]{};
        std::size_t   m_bufferUsed{ 0 };
        std::uint64_t m_bitCount{ 0 };
    };

    // 소문자 16진 64자.
    inline std::string ToHex(const Sha256Digest& digest)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string s;
        s.reserve(64);
        for (std::uint8_t b : digest)
        {
            s.push_back(kHex[b >> 4]);
            s.push_back(kHex[b & 0x0Fu]);
        }
        return s;
    }
} // namespace Hash
