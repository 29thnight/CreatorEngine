export module Creator.Core.Uuid;

import std;

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
export namespace uuid
{
    // ── SHA-1 (FIPS 180-4) ─────────────────────────────────────────────
    // 외부 의존을 없애는 것이 이 작업의 목적이므로 직접 둔다.
    // 상태는 h[5]와 64바이트 블록 버퍼뿐이다.
    class sha1
    {
      public:
        void update(const void* data, std::size_t length) noexcept
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            bit_count_ += static_cast<std::uint64_t>(length) * 8;

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

        // 20바이트 다이제스트를 낸다. 호출한 뒤 이 객체는 다시 쓰지 않는다.
        void finish(std::uint8_t out[20]) noexcept
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
                buffer_[56 + i] = static_cast<std::uint8_t>((bits >> (56 - i * 8)) & 0xFF);
            }
            transform(buffer_);

            for (int i = 0; i < 5; ++i)
            {
                out[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
                out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
                out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
                out[i * 4 + 3] = static_cast<std::uint8_t>((state_[i]) & 0xFF);
            }
        }

      private:
        static constexpr std::size_t block_size_ = 64;

        static std::uint32_t rotate_left(std::uint32_t v, int n) noexcept { return (v << n) | (v >> (32 - n)); }

        void transform(const std::uint8_t block[block_size_]) noexcept
        {
            std::uint32_t w[80];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                       (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                       (static_cast<std::uint32_t>(block[i * 4 + 3]));
            }
            for (int i = 16; i < 80; ++i)
            {
                w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
            for (int i = 0; i < 80; ++i)
            {
                std::uint32_t f, k;
                if (i < 20)
                {
                    f = (b & c) | (~b & d);
                    k = 0x5A827999u;
                }
                else if (i < 40)
                {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1u;
                }
                else if (i < 60)
                {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDCu;
                }
                else
                {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6u;
                }

                const std::uint32_t temp = rotate_left(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rotate_left(b, 30);
                b = a;
                a = temp;
            }
            state_[0] += a;
            state_[1] += b;
            state_[2] += c;
            state_[3] += d;
            state_[4] += e;
        }

        std::uint32_t state_[5]{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
        std::uint8_t buffer_[block_size_]{};
        std::size_t buffer_used_{0};
        std::uint64_t bit_count_{0};
    };

    // ── uuid ───────────────────────────────────────────────────────────
    // boost::uuids::uuid는 `std::uint8_t data[16]` 하나뿐인 POD였다.
    // ★ ModelLoader가 이 값을 파일에 통째로 write/read하므로(595·967행)
    //   크기와 배치가 같아야 한다. static_assert로 붙들어 둔다.
    struct uuid16
    {
        std::array<std::uint8_t, 16> data_{};

        friend bool operator==(const uuid16& a, const uuid16& b) noexcept { return a.data_ == b.data_; }
        friend auto operator<=>(const uuid16& a, const uuid16& b) noexcept { return a.data_ <=> b.data_; }

        bool is_nil() const noexcept
        {
            for (std::uint8_t b : data_)
            {
                if (b != 0)
                {
                    return false;
                }
            }
            return true;
        }
    };
    static_assert(sizeof(uuid16) == 16, "boost::uuids::uuid와 배치가 같아야 한다");

    inline constexpr uuid16 nil() noexcept
    {
        return uuid16{};
    }

    // 표준 표기: 8-4-4-4-12 소문자 16진.
    inline std::string to_string(const uuid16& u)
    {
        static constexpr char hex_digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(36);
        for (std::size_t i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                s.push_back('-');
            }
            s.push_back(hex_digits[u.data_[i] >> 4]);
            s.push_back(hex_digits[u.data_[i] & 0x0F]);
        }
        return s;
    }

    // v5: SHA-1(namespace ‖ name). boost의 hash_to_uuid와 같은 절차.
    inline uuid16 from_name(const uuid16& ns, std::string_view name) noexcept
    {
        sha1 sha;
        sha.update(ns.data_.data(), ns.data_.size());
        sha.update(name.data(), name.size());

        std::uint8_t digest[20];
        sha.finish(digest);

        uuid16 u;
        std::memcpy(u.data_.data(), digest, 16);
        u.data_[8] = static_cast<std::uint8_t>((u.data_[8] & 0x3F) | 0x80);
        u.data_[6] = static_cast<std::uint8_t>((u.data_[6] & 0x0F) | (5 << 4));
        return u;
    }

    // ── 파싱 ───────────────────────────────────────────────────────────
    // boost::uuids::string_generator::operator()를 1:1로 옮긴 것이다.
    // 받아들이는 집합과 거절하는 집합이 boost와 정확히 같아야 한다 —
    // 더 너그러우면 예전에 튕기던 쓰레기 문자열이 조용히 통과하고, 더
    // 깐깐하면 읽히던 자산이 갑자기 안 읽힌다. 둘 다 축출이 낼 일이 아니다.
    //
    // boost가 받는 네 가지 형태:
    //   0123456789abcdef0123456789abcdef
    //   01234567-89ab-cdef-0123-456789abcdef
    //   {01234567-89ab-cdef-0123-456789abcdef}
    //   {0123456789abcdef0123456789abcdef}
    //
    // ★ 하이픈 규칙이 핵심이다. 다섯째 바이트 자리(i==4)에서만 하이픈이
    //   있는지 보고, 있었다면 i==6·8·10에도 반드시 있어야 한다("전부 아니면
    //   전무"). 그 밖의 자리에 낀 하이픈은 16진수 자리에서 걸린다.
    //   예전 구현은 하이픈을 아무 데나 두어도 무시했는데, 그러면
    //   "0123-456789abcdef0123456789abcdef" 같은 것을 boost는 튕기고
    //   이쪽은 받아 버린다.
    //
    // 대문자 16진도 받는다. 디스크에 옛 기록기가 남긴 "{대문자}" 형태가
    // 52건 섞여 있고, boost도 받아 왔다.
    namespace detail
    {
        struct parse_error
        {
            int position_{0};
            const char* message_{""};
        };

        // 성공하면 true. 실패하면 err에 boost와 같은 위치·사유를 담는다.
        inline bool parse_into(std::string_view s, uuid16& out, parse_error& err) noexcept
        {
            std::size_t idx = 0;
            int position = 0;

            // boost의 get_next_char와 같다 — 끝이면 그 자리(position)를 알리고,
            // 아니면 ipos를 올린 뒤 한 글자를 낸다.
            auto next = [&](char& c) noexcept -> bool
            {
                if (idx == s.size())
                {
                    err = {position, "unexpected end of input"};
                    return false;
                }
                ++position;
                c = s[idx++];
                return true;
            };

            // boost의 get_value. 위치는 position - 1을 쓴다.
            auto value = [&](char c, int at, std::uint8_t& v) noexcept -> bool
            {
                if (c >= '0' && c <= '9')
                {
                    v = static_cast<std::uint8_t>(c - '0');
                    return true;
                }
                if (c >= 'a' && c <= 'f')
                {
                    v = static_cast<std::uint8_t>(c - 'a' + 10);
                    return true;
                }
                if (c >= 'A' && c <= 'F')
                {
                    v = static_cast<std::uint8_t>(c - 'A' + 10);
                    return true;
                }
                err = {at, "hex digit expected"};
                return false;
            };

            char c;
            if (!next(c))
            {
                return false;
            }

            const bool hasOpenBrace = (c == '{');
            if (hasOpenBrace)
            {
                if (!next(c))
                {
                    return false;
                }
            }

            bool hasDashes = false;
            uuid16 u;

            for (int i = 0; i < 16; ++i)
            {
                if (i != 0)
                {
                    if (!next(c))
                    {
                        return false;
                    }
                }

                if (i == 4)
                {
                    hasDashes = (c == '-');
                    if (hasDashes)
                    {
                        if (!next(c))
                        {
                            return false;
                        }
                    }
                }
                else if (i == 6 || i == 8 || i == 10)
                {
                    // 하이픈을 쓰기로 했으면 이 자리마다 있어야 한다.
                    if (hasDashes)
                    {
                        if (c == '-')
                        {
                            if (!next(c))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            err = {position - 1, "dash expected"};
                            return false;
                        }
                    }
                }

                std::uint8_t hi, lo;
                if (!value(c, position - 1, hi))
                {
                    return false;
                }
                if (!next(c))
                {
                    return false;
                }
                if (!value(c, position - 1, lo))
                {
                    return false;
                }

                u.data_[i] = static_cast<std::uint8_t>((hi << 4) | lo);
            }

            if (hasOpenBrace)
            {
                if (!next(c))
                {
                    return false;
                }
                if (c != '}')
                {
                    err = {position - 1, "closing brace expected"};
                    return false;
                }
            }

            // 32자리를 다 읽고도 남은 것이 있으면 잘못된 uuid다.
            if (idx != s.size())
            {
                err = {position, "unexpected extra input"};
                return false;
            }

            out = u;
            return true;
        }
    } // namespace detail

    // 던지지 않는 파싱. 받아들이는 집합은 boost와 같다.
    inline bool try_parse(std::string_view s, uuid16& out) noexcept
    {
        detail::parse_error err;
        return detail::parse_into(s, out, err);
    }

    // 던지는 파싱. boost::uuids::string_generator가 std::runtime_error를
    // 던졌고 호출부 여섯 자리(.meta·모델 파일·씬 yaml) 어디도 잡지 않는다 —
    // 축출로 거동이 바뀌지 않도록 예외 형과 메시지 문안까지 맞췄다.
    inline uuid16 parse(std::string_view s)
    {
        uuid16 u;
        detail::parse_error err;
        if (!detail::parse_into(s, u, err))
        {
            throw std::runtime_error("Invalid UUID string at position " + std::to_string(err.position_) + ": " +
                                     err.message_);
        }
        return u;
    }
} // namespace uuid
