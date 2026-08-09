#pragma once
// UUID v5(RFC 4122) 자체 구현 — boost/uuid를 대신한다.
//
// ── 왜 자체 구현인가 ──
//   저장소가 boost에서 쓰던 것은 FileGuid 하나뿐이었다. 그 하나 때문에
//   include 디렉토리 190개와 컴파일된 lib 10개(atomic·chrono·regex·
//   serialization 등)를 지고 있었다.
//
//   덤으로 지뢰 하나가 사라진다. boost/uuid는 std::numeric_limits<T>::max()를
//   부르는데 이 저장소는 NOMINMAX를 정의하지 않아(DX12MeshCache.h 주석),
//   windows.h가 먼저 들어온 TU에서 max 매크로에 치환돼 구문 오류가 났다.
//   유니티 빌드가 그것을 가려 왔고 비유니티에서 186건으로 터졌다.
//
// ── 바이트 동일성 ──
//   이 구현이 내는 값은 boost 1.86의 name_generator(= name_generator_sha1)와
//   바이트 단위로 같아야 한다. .meta 612개와 .creator 씬이 그 값을 참조하므로
//   한 바이트만 어긋나도 모든 에셋 참조가 끊긴다.
//
//   boost의 절차(detail/basic_name_generator.hpp: hash_to_uuid)는 이렇다:
//     SHA-1(namespace 16바이트 ‖ name 바이트)의 앞 16바이트를 취하고
//     [8] &= 0x3F | 0x80   (변형 비트: 0b10xxxxxx)
//     [6] &= 0x0F | (5<<4) (버전 5)
//   RFC 4122 v5 그대로다.
//
//   교체 전 세 갈래로 확인했다 — RFC 공표 벡터, boost와 같은 입력 직접 대조
//   517건(SHA-1 블록 경계·빈 문자열·다바이트·무작위 500건 포함), 그리고
//   디스크의 .meta 골든 354건. 마지막 것이 가장 강하다. 지금 링크된 boost가
//   아니라 '과거의 boost가 실제로 내서 파일에 적어 둔 값'과의 동치이기 때문이다.
//
//   SHA-1 라운드 상수를 한 비트 틀어 두면 셋 다 발화하는 것도 확인했다.
//   발화하지 않는 검출기를 통과로 읽지 않기 위해서다.
//
// ── 헤더 온리인 이유 ──
//   TypeTrait.h가 Core.Minimal.h를 통해 거의 전 저장소에 퍼져 있다. .cpp를
//   더하면 링크 그래프와 vcxproj를 건드려야 하는데, 얻는 것보다 위험이 크다.

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Uuid
{
    // ── SHA-1 (FIPS 180-4) ─────────────────────────────────────────────
    // 외부 의존을 없애는 것이 이 작업의 목적이므로 직접 둔다.
    // 상태는 h[5]와 64바이트 블록 버퍼뿐이다.
    class Sha1
    {
    public:
        void Update(const void* data, size_t length) noexcept
        {
            const auto* p = static_cast<const uint8_t*>(data);
            m_bitCount += static_cast<uint64_t>(length) * 8;

            while (length > 0)
            {
                const size_t room = kBlockSize - m_bufferUsed;
                const size_t take = (length < room) ? length : room;
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

        // 20바이트 다이제스트를 낸다. 호출한 뒤 이 객체는 다시 쓰지 않는다.
        void Finish(uint8_t out[20]) noexcept
        {
            const uint64_t bits = m_bitCount;

            const uint8_t pad = 0x80;
            Update(&pad, 1);
            while (m_bufferUsed != 56)
            {
                const uint8_t zero = 0;
                Update(&zero, 1);
            }
            // 길이는 빅엔디언 64비트. Update를 거치면 m_bitCount가 또 늘므로
            // 버퍼에 직접 적는다.
            for (int i = 0; i < 8; ++i)
            {
                m_buffer[56 + i] = static_cast<uint8_t>((bits >> (56 - i * 8)) & 0xFF);
            }
            Transform(m_buffer);

            for (int i = 0; i < 5; ++i)
            {
                out[i * 4 + 0] = static_cast<uint8_t>((m_h[i] >> 24) & 0xFF);
                out[i * 4 + 1] = static_cast<uint8_t>((m_h[i] >> 16) & 0xFF);
                out[i * 4 + 2] = static_cast<uint8_t>((m_h[i] >> 8) & 0xFF);
                out[i * 4 + 3] = static_cast<uint8_t>((m_h[i]) & 0xFF);
            }
        }

    private:
        static constexpr size_t kBlockSize = 64;

        static uint32_t Rotl(uint32_t v, int n) noexcept
        {
            return (v << n) | (v >> (32 - n));
        }

        void Transform(const uint8_t block[kBlockSize]) noexcept
        {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24)
                     | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                     | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
                     | (static_cast<uint32_t>(block[i * 4 + 3]));
            }
            for (int i = 16; i < 80; ++i)
            {
                w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4];
            for (int i = 0; i < 80; ++i)
            {
                uint32_t f, k;
                if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
                else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }

                const uint32_t temp = Rotl(a, 5) + f + e + k + w[i];
                e = d; d = c; c = Rotl(b, 30); b = a; a = temp;
            }
            m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d; m_h[4] += e;
        }

        uint32_t m_h[5]{ 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
        uint8_t  m_buffer[kBlockSize]{};
        size_t   m_bufferUsed{ 0 };
        uint64_t m_bitCount{ 0 };
    };

    // ── uuid ───────────────────────────────────────────────────────────
    // boost::uuids::uuid는 `uint8_t data[16]` 하나뿐인 POD였다.
    // ★ ModelLoader가 이 값을 파일에 통째로 write/read하므로(595·967행)
    //   크기와 배치가 같아야 한다. static_assert로 붙들어 둔다.
    struct Uuid16
    {
        std::array<uint8_t, 16> data{};

        friend bool operator==(const Uuid16& a, const Uuid16& b) noexcept
        {
            return a.data == b.data;
        }
        friend auto operator<=>(const Uuid16& a, const Uuid16& b) noexcept
        {
            return a.data <=> b.data;
        }

        bool IsNil() const noexcept
        {
            for (uint8_t b : data) { if (b != 0) return false; }
            return true;
        }
    };
    static_assert(sizeof(Uuid16) == 16, "boost::uuids::uuid와 배치가 같아야 한다");

    inline constexpr Uuid16 Nil() noexcept { return Uuid16{}; }

    // 표준 표기: 8-4-4-4-12 소문자 16진.
    inline std::string ToString(const Uuid16& u)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string s;
        s.reserve(36);
        for (size_t i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back('-');
            s.push_back(kHex[u.data[i] >> 4]);
            s.push_back(kHex[u.data[i] & 0x0F]);
        }
        return s;
    }

    // v5: SHA-1(namespace ‖ name). boost의 hash_to_uuid와 같은 절차.
    inline Uuid16 FromName(const Uuid16& ns, std::string_view name) noexcept
    {
        Sha1 sha;
        sha.Update(ns.data.data(), ns.data.size());
        sha.Update(name.data(), name.size());

        uint8_t digest[20];
        sha.Finish(digest);

        Uuid16 u;
        std::memcpy(u.data.data(), digest, 16);
        u.data[8] = static_cast<uint8_t>((u.data[8] & 0x3F) | 0x80);
        u.data[6] = static_cast<uint8_t>((u.data[6] & 0x0F) | (5 << 4));
        return u;
    }

    // 던지지 않는 파싱. 중괄호 유무·대소문자·하이픈 위치를 가리지 않는다.
    // (디스크에는 두 형식이 섞여 있다 — 지금 기록기의 맨 소문자와,
    //  옛 기록기의 "{대문자}" 형태 52건)
    inline bool TryParse(std::string_view s, Uuid16& out) noexcept
    {
        if (s.size() >= 2 && s.front() == '{' && s.back() == '}')
        {
            s.remove_prefix(1);
            s.remove_suffix(1);
        }

        auto nibble = [](char c, uint8_t& v) noexcept -> bool
        {
            if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0');      return true; }
            if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
            if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
            return false;
        };

        Uuid16 tmp;
        size_t byteIndex = 0;
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] == '-') { ++i; continue; }
            if (byteIndex >= 16) return false;
            if (i + 1 >= s.size()) return false;
            uint8_t hi, lo;
            if (!nibble(s[i], hi) || !nibble(s[i + 1], lo)) return false;
            tmp.data[byteIndex++] = static_cast<uint8_t>((hi << 4) | lo);
            i += 2;
        }
        if (byteIndex != 16) return false;
        out = tmp;
        return true;
    }

    // 던지는 파싱. boost::uuids::string_generator가 std::runtime_error를
    // 던졌고 호출부 여섯 자리(.meta·모델 파일·씬 yaml) 어디도 잡지 않는다 —
    // 축출로 거동이 바뀌지 않도록 그대로 둔다.
    inline Uuid16 Parse(std::string_view s)
    {
        Uuid16 u;
        if (!TryParse(s, u))
        {
            throw std::runtime_error("Invalid UUID string: " + std::string(s));
        }
        return u;
    }
} // namespace Uuid
