// PakLib.hpp — header-only minimal packer/loader (Unreal Pak 스타일)
// Feature: chunked container, optional compression (LZ4 hook), AES-256-CTR encryption via Windows CNG (bcrypt.h),
//          end-of-file index with SHA-256 integrity, UTF-8 paths, streaming I/O.
// Target: MSVC / C++20 / Windows 10+
//
// NOTE about LZ4: 이 파일은 LZ4를 외부 라이브러리로 연동하는 hook을 제공합니다.
//  - LZ4 사용: 프로젝트에 LZ4를 추가하고, 아래 Pak::Compression::LZ4Codec 구현의 TODO 표시된 부분을 채우세요.
//  - 임시로 "NoCompression"을 사용하면 암호화/포맷/인덱스 흐름은 그대로 테스트할 수 있습니다.
//  - AES-256-CTR은 WinCNG(BCrypt)로 완전 구현되어 있습니다 (OpenSSL 불필요).
//
// MIT License
//
// ---------------------------  PUBLIC API OVERVIEW  ---------------------------
// namespace Pak { using u8 = std::uint8_t; ... }
//
// struct BuildOptions { std::uint32_t chunkSize = 256*1024; bool encrypt = true; bool compress = true; };
// struct OpenOptions  { std::optional<std::array<u8,32>> key; };
//
// // 빌더 사용 예시
// Pak::Builder b{"/path/to/out.pak", BuildOptions{}};
// b.setKeyFromHex("001122...64 hex chars...");
// b.addFile("virtual/dir/hero_diffuse.png", "C:/assets/hero_diffuse.png");
// b.addMemory("data/readme.txt", std::as_bytes(std::span{"hello"}));
// b.finish();
//
// // 로더 사용 예시
// Pak::Archive a{"/path/to/out.pak", OpenOptions{ .key = b.key() }};
// auto bytes = a.readAll("data/readme.txt");
// a.extractToFile("virtual/dir/hero_diffuse.png", L"C:/dump/hero_diffuse.png");
// for (auto&& e : a.list()) { std::println("{} ({} B)", e.path, e.uncompressedSize); }
// -----------------------------------------------------------------------------

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <span>
#include <filesystem>
#include <fstream>
#include <array>
#include <variant>
#include <unordered_map>
#include <stdexcept>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ranges>

#pragma warning(disable : 4996)
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace Pak {
    using u8 = std::uint8_t; using u16 = std::uint16_t; using u32 = std::uint32_t; using u64 = std::uint64_t;

    // ------------------------------- Utility -----------------------------------
    struct WinError : std::runtime_error { using std::runtime_error::runtime_error; };
    [[noreturn]] inline void fail(std::string_view msg) { throw WinError{ std::string(msg) }; }

    inline void ensure(bool ok, std::string_view msg) { if (!ok) fail(msg); }

    // FNV-1a 64 (경량 경로 해시)
    inline u64 fnv1a64(std::string_view s) {
        const u64 FNV_OFFSET = 14695981039346656037ull;
        const u64 FNV_PRIME = 1099511628211ull;
        u64 h = FNV_OFFSET;
        for (unsigned char c : s) { h ^= c; h *= FNV_PRIME; }
        return h;
    }

    inline std::wstring utf8_to_wide(std::string_view s) {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w; w.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
        return w;
    }
    inline std::string wide_to_utf8(std::wstring_view w) {
        if (w.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s; s.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
        return s;
    }

    // ----------------------------- AES-256-CTR ----------------------------------
    namespace Crypto {
        struct Aes256Ctr {
            BCRYPT_ALG_HANDLE hAlg{}; BCRYPT_KEY_HANDLE hKey{}; std::array<u8, 16> iv{}; // 128-bit counter/nonce
            ~Aes256Ctr() { if (hKey) BCryptDestroyKey(hKey); if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0); }

            static std::array<u8, 16> makeCtrIV(const u8 salt[16], u64 fileId) {
                std::array<u8, 16> out{}; // 0..7: salt[0..7], 8..15: salt[8..15] xor fileId
                std::memcpy(out.data(), salt, 16);
                u64 ctr = fileId; // per-file unique start counter
                for (int i = 0; i < 8; i++) out[8 + i] ^= (u8)((ctr >> (i * 8)) & 0xFF);
                return out;
            }

            void init(const std::array<u8, 32>& key, std::array<u8, 16> iv_) {
                NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0); if (st < 0) fail("BCryptOpenAlgorithmProvider");
                st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, (ULONG)std::wcslen(BCRYPT_CHAIN_MODE_ECB) * sizeof(wchar_t), 0);
                if (st < 0) fail("BCryptSetProperty ECB");
                DWORD objLen = 0, r = 0; st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &r, 0); if (st < 0) fail("GetProperty OBJECT_LENGTH");
                std::unique_ptr<u8[]> obj(new u8[objLen]{});
                st = BCryptGenerateSymmetricKey(hAlg, &hKey, obj.get(), objLen, (PUCHAR)key.data(), (ULONG)key.size(), 0);
                if (st < 0) fail("BCryptGenerateSymmetricKey");
                iv = iv_;
            }

            // CTR = XOR(Plain, E_k(IV++))
            void crypt_inplace(u8* data, size_t len) {
                std::array<u8, 16> ctr = iv; u8 keystream[16]; DWORD out = 0; u8 block[16]{};
                size_t off = 0;
                while (off < len) {
                    // ECB encrypt ctr -> keystream
                    std::memcpy(block, ctr.data(), 16);
                    NTSTATUS st = BCryptEncrypt(hKey, block, 16, nullptr, nullptr, 0, keystream, 16, &out, 0);
                    if (st < 0) fail("BCryptEncrypt(CTR)");
                    size_t n = std::min<size_t>(16, len - off);
                    for (size_t i = 0; i < n; i++) data[off + i] ^= keystream[i];
                    off += n;
                    // increment ctr (little-endian increment on last 8 bytes)
                    for (int i = 15; i >= 8; i--) { if (++ctr[i]) break; }
                }
            }
        };

        inline std::array<u8, 32> sha256(std::span<const u8> data) {
            BCRYPT_ALG_HANDLE hAlg{}; BCRYPT_HASH_HANDLE hHash{}; NTSTATUS st;
            st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0); if (st < 0) fail("Open SHA256");
            DWORD objLen = 0, r = 0; st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &r, 0); if (st < 0) fail("Get OBJLEN");
            std::unique_ptr<u8[]> obj(new u8[objLen]{});
            st = BCryptCreateHash(hAlg, &hHash, obj.get(), objLen, nullptr, 0, 0); if (st < 0) fail("CreateHash");
            st = BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0); if (st < 0) fail("HashData");
            std::array<u8, 32> digest{}; st = BCryptFinishHash(hHash, digest.data(), (ULONG)digest.size(), 0); if (st < 0) fail("FinishHash");
            BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return digest;
        }
    }

    // ---------------------------- Compression API -------------------------------
    namespace Compression {
#ifdef PAK_WITH_LZ4
        extern "C" {
#   include "lz4.h"
        }
#endif

        struct ICodec {
            virtual ~ICodec() = default;
            virtual std::string name() const = 0;
            virtual std::vector<u8> compress(std::span<const u8> src) = 0;
            virtual std::vector<u8> decompress(std::span<const u8> src, size_t uncompressedSize) = 0;
        };

        struct NoCompression final : ICodec {
            std::string name() const override { return "none"; }
            std::vector<u8> compress(std::span<const u8> src) override { return std::vector<u8>(src.begin(), src.end()); }
            std::vector<u8> decompress(std::span<const u8> src, size_t) override { return std::vector<u8>(src.begin(), src.end()); }
        };

        // LZ4 hook (외부 라이브러리 연결 지점)
        struct LZ4Codec final : ICodec {
            std::string name() const override { return "lz4"; }

            std::vector<u8> compress(std::span<const u8> src) override {
#ifdef PAK_WITH_LZ4
                const int srcSize = static_cast<int>(src.size());
                const int maxDst = LZ4_compressBound(srcSize);
                std::vector<u8> out(static_cast<size_t>(maxDst));
                const int n = LZ4_compress_default(
                    reinterpret_cast<const char*>(src.data()),
                    reinterpret_cast<char*>(out.data()),
                    srcSize, maxDst
                );
                ensure(n > 0, "LZ4 compress failed");
                out.resize(static_cast<size_t>(n));
                return out;
#else
                // LZ4 미연동 시, 패스스루 (빌더/리더가 동일 경로를 사용해야 함)
                return std::vector<u8>(src.begin(), src.end());
#endif
            }

            std::vector<u8> decompress(std::span<const u8> src, size_t uncompressedSize) override {
#ifdef PAK_WITH_LZ4
                std::vector<u8> out(uncompressedSize);
                const int n = LZ4_decompress_safe(
                    reinterpret_cast<const char*>(src.data()),
                    reinterpret_cast<char*>(out.data()),
                    static_cast<int>(src.size()),
                    static_cast<int>(out.size())
                );
                ensure(n >= 0, "LZ4 decompress failed");
                if (static_cast<size_t>(n) != uncompressedSize) out.resize(static_cast<size_t>(n));
                return out;
#else
                // LZ4 미연동 시, 패스스루
                std::vector<u8> out(src.begin(), src.end());
                if (out.size() != uncompressedSize) out.resize(uncompressedSize);
                return out;
#endif
            }
        };
    }

    // ------------------------------ File Format ---------------------------------
    // [Header 64 bytes]
    //   magic[4] = 'P''A''K''1'
    //   version u32 = 1
    //   flags   u32 : bit0=encrypted, bit1=compressed
    //   chunkSize u32
    //   reserved u32
    //   dataStart u64  (첫 데이터 블록 오프셋)
    //   indexOfs  u64
    //   indexSize u64
    //   salt[16]  (AES IV 파생용)
    // [Data ...] (파일별 chunk + per-file chunkTable)
    // [Index]
    //   IndexHeader {
    //      fileCount u32; compAlgo u8; encAlgo u8; reserved[2]
    //      indexHash[32] (IndexEntries 직전~끝까지 SHA-256)
    //   }
    //   repeated IndexEntry {
    //      pathLen u16; u8 path[pathLen UTF-8]
    //      u64 pathHash; u64 fileId; u64 uncompressedSize; u64 dataOfs; u32 chunkSize; u32 chunkCount; u8 flags;
    //   }
    //   for each file: chunkCount times: u32 chunkCompSize; u32 chunkUncompSize; u64 chunkOfs;

#pragma pack(push,1)
    struct Header {
        char magic[4] = { 'P','A','K','1' };
        u32 version = 1;
        u32 flags = 0;          // bit0=enc, bit1=comp
        u32 chunkSize = 256 * 1024;
        u32 reserved = 0;
        u64 dataStart = 0;
        u64 indexOfs = 0;
        u64 indexSize = 0;
        u8  salt[16]{};         // random
    };
    struct IndexHeader {
        u32 fileCount = 0; u8 compAlgo = 0; u8 encAlgo = 1; u8 reserved[2]{}; u8 indexHash[32]{};
    };
#pragma pack(pop)

    enum class CompAlgo : u8 { None = 0, LZ4 = 1 };
    enum class EncAlgo : u8 { None = 0, AES256CTR = 1 };

    struct ChunkInfo { u32 compSize; u32 uncompSize; u64 ofs; };

    struct Entry {
        std::string path; u64 pathHash{}; u64 fileId{}; u64 uncompressedSize{}; u64 dataOfs{}; u32 chunkSize{}; u32 chunkCount{}; u8 flags{}; // bit0=enc, bit1=comp
        std::vector<ChunkInfo> chunks; // per-file chunk table
    };

    // ------------------------------ Builder -------------------------------------
    struct BuildOptions { u32 chunkSize = 256 * 1024; bool encrypt = true; bool compress = true; CompAlgo comp = CompAlgo::LZ4; };

    class Builder {
        std::filesystem::path m_out;
        BuildOptions m_opt;
        std::unique_ptr<Compression::ICodec> m_codec;
        std::optional<std::array<u8, 32>> m_key; // AES-256
        Header m_header{};
        std::vector<Entry> m_entries;

    public:
        explicit Builder(std::filesystem::path outPak, BuildOptions opt = {}) : m_out(std::move(outPak)), m_opt(opt) {
            if (m_opt.comp == CompAlgo::LZ4) m_codec = std::make_unique<Compression::LZ4Codec>();
            else m_codec = std::make_unique<Compression::NoCompression>();
            // random salt
            BCryptGenRandom(NULL, m_header.salt, sizeof(m_header.salt), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            m_header.chunkSize = m_opt.chunkSize;
        }

        const std::optional<std::array<u8, 32>>& key() const { return m_key; }

        void setKey(const std::array<u8, 32>& k) { m_key = k; }
        void setKeyFromHex(std::string_view hex) {
            ensure(hex.size() == 64, "key hex must be 64 chars for 32 bytes");
            std::array<u8, 32> k{}; for (size_t i = 0; i < 32; i++) { unsigned v; std::sscanf(std::string(hex.substr(2 * i, 2)).c_str(), "%02x", &v); k[i] = (u8)v; }
            m_key = k;
        }

        void addFile(std::string_view virtualPath, const std::filesystem::path& srcFile) {
            std::ifstream in(srcFile, std::ios::binary); ensure(in.good(), "open src file");
            in.seekg(0, std::ios::end); size_t size = (size_t)in.tellg(); in.seekg(0);
            std::vector<u8> buf(size); in.read((char*)buf.data(), size);
            addMemory(virtualPath, std::as_bytes(std::span{ buf }));
        }

        void addMemory(std::string_view virtualPath, std::span<const std::byte> bytes) {
            Entry e{}; e.path = std::string(virtualPath); e.pathHash = fnv1a64(e.path); e.fileId = (u64)m_entries.size() + 1; e.uncompressedSize = bytes.size(); e.chunkSize = m_opt.chunkSize;
            e.flags = 0; if (m_opt.encrypt && m_key) e.flags |= 1; if (m_opt.compress && m_codec->name() != "none") e.flags |= 2;
            // chunk plan
            e.chunkCount = (u32)((bytes.size() + e.chunkSize - 1) / e.chunkSize);
            e.chunks.reserve(e.chunkCount);
            m_entries.emplace_back(std::move(e));
            m_payloads.emplace_back(std::vector<u8>((const u8*)bytes.data(), (const u8*)bytes.data() + bytes.size()));
        }

        void finish() {
            ensure(!m_entries.empty(), "no entries");
            // open output
            std::FILE* fp = nullptr; _wfopen_s(&fp, m_out.wstring().c_str(), L"wb+"); ensure(fp, "open out pak");
            auto write = [&](const void* p, size_t n) { if (std::fwrite(p, 1, n, fp) != n) fail("write pak"); };
            auto tell = [&]() { return (u64)_ftelli64(fp); };
            auto seek = [&](u64 ofs) { _fseeki64(fp, ofs, SEEK_SET); };

            // placeholder header
            m_header.flags = 0; if (m_opt.encrypt && m_key) m_header.flags |= 1; if (m_opt.compress && m_codec->name() != "none") m_header.flags |= 2;
            write(&m_header, sizeof(m_header));
            m_header.dataStart = tell();

            // per-file write (chunk -> optional compress -> optional encrypt)
            for (size_t i = 0; i < m_entries.size(); ++i) {
                Entry& e = m_entries[i]; auto& mem = m_payloads[i];
                e.dataOfs = tell();
                size_t off = 0; u32 chunkIdx = 0;
                // per-file CTR iv
                std::array<u8, 16> iv{}; if (m_opt.encrypt && m_key) { iv = Crypto::Aes256Ctr::makeCtrIV(m_header.salt, e.fileId); }
                while (off < mem.size()) {
                    size_t n = std::min<size_t>(e.chunkSize, mem.size() - off);
                    std::span<const u8> src{ mem.data() + off, n };
                    std::vector<u8> out = ((e.flags & 2) ? m_codec->compress(src) : std::vector<u8>(src.begin(), src.end()));
                    // encrypt in-place
                    if (e.flags & 1) {
                        Crypto::Aes256Ctr aes; aes.init(m_key.value(), iv); aes.crypt_inplace(out.data(), out.size());
                        // advance CTR by blocks consumed
                        size_t blocks = (out.size() + 15) / 16; for (size_t b = 0; b < blocks; b++) { for (int k = 15; k >= 8; k--) { if (++iv[k]) break; } }
                    }
                    u64 pos = tell(); write(out.data(), out.size());
                    e.chunks.push_back(ChunkInfo{ (u32)out.size(), (u32)n, pos });
                    off += n; chunkIdx++;
                }
                // write chunk table immediately after data (per-file localized)
                u64 tablePos = tell();
                for (auto& c : e.chunks) write(&c, sizeof(ChunkInfo));
                // we keep ChunkInfo.ofs = data offset; table position is not required at runtime since we will read it from index
                (void)tablePos;
            }

            // build & write index
            m_header.indexOfs = tell();
            IndexHeader ih{}; ih.fileCount = (u32)m_entries.size(); ih.compAlgo = (m_codec->name() == "lz4" ? (u8)CompAlgo::LZ4 : (u8)CompAlgo::None); ih.encAlgo = (m_header.flags & 1) ? (u8)EncAlgo::AES256CTR : (u8)EncAlgo::None;
            // write temp ih (hash zero)
            write(&ih, sizeof(ih));
            u64 hashStart = tell();
            for (auto& e : m_entries) {
                u16 plen = (u16)e.path.size(); write(&plen, sizeof(plen)); write(e.path.data(), plen);
                write(&e.pathHash, sizeof(e.pathHash)); write(&e.fileId, sizeof(e.fileId)); write(&e.uncompressedSize, sizeof(e.uncompressedSize));
                write(&e.dataOfs, sizeof(e.dataOfs)); write(&e.chunkSize, sizeof(e.chunkSize)); write(&e.chunkCount, sizeof(e.chunkCount)); write(&e.flags, sizeof(e.flags));
                for (auto& c : e.chunks) write(&c, sizeof(ChunkInfo));
            }
            u64 hashEnd = tell();
            {
                // compute SHA256 over [hashStart..hashEnd)
                _fseeki64(fp, hashStart, SEEK_SET);
                std::vector<u8> idx; idx.resize((size_t)(hashEnd - hashStart));
                fread(idx.data(), 1, idx.size(), fp);
                auto digest = Crypto::sha256(idx);
                std::memcpy(ih.indexHash, digest.data(), 32);
            }
            // rewrite ih with hash
            seek(m_header.indexOfs);
            write(&ih, sizeof(ih));

            // ✅ 인덱스 전체 길이는 처음 기록한 index 시작 위치부터 '본문의 끝(hashEnd)'까지여야 함
            m_header.indexSize = hashEnd - m_header.indexOfs;

            // (선택) 포인터 복구 — 이후 동작엔 큰 영향 없지만 깔끔하게 끝 위치로 되돌림
            seek(hashEnd);

            // finalize header
            seek(0); write(&m_header, sizeof(m_header));
            std::fclose(fp);
        }

    private:
        std::vector<std::vector<u8>> m_payloads; // source data per entry
    };

    // ------------------------------- Archive ------------------------------------
    struct OpenOptions { std::optional<std::array<u8, 32>> key; };

    class Archive {
        std::filesystem::path m_path; Header m_hdr{}; IndexHeader m_ih{}; std::vector<Entry> m_entries; std::unordered_map<u64, size_t> m_hashToIndex; std::optional<std::array<u8, 32>> m_key;
    public:
        explicit Archive(std::filesystem::path pak, OpenOptions opt = {}) : m_path(std::move(pak)), m_key(std::move(opt.key)) {
            std::FILE* fp = nullptr; _wfopen_s(&fp, m_path.wstring().c_str(), L"rb"); ensure(fp, "open pak");
            auto read = [&](void* p, size_t n) { if (std::fread(p, 1, n, fp) != n) fail("read pak"); };
            auto seek = [&](u64 ofs) { _fseeki64(fp, ofs, SEEK_SET); };
            read(&m_hdr, sizeof(m_hdr)); ensure(std::string_view(m_hdr.magic, 4) == "PAK1", "bad magic");
            // index
            seek(m_hdr.indexOfs); read(&m_ih, sizeof(m_ih));
            //ensure(m_hdr.indexSize >= sizeof(IndexHeader), "bad indexSize");
            // verify index hash
            {
                u64 hashStart = _ftelli64(fp);
                std::vector<u8> idx; idx.resize((size_t)(m_hdr.indexSize - sizeof(IndexHeader)));
                read(idx.data(), idx.size());
                auto digest = Crypto::sha256(idx);
                ensure(std::memcmp(digest.data(), m_ih.indexHash, 32) == 0, "index hash mismatch");
                // rewind to parse entries
                seek(hashStart);
            }
            // parse entries
            for (u32 i = 0; i < m_ih.fileCount; i++) {
                u16 plen; read(&plen, sizeof(plen)); std::string path; path.resize(plen); read(path.data(), plen);
                Entry e{}; e.path = std::move(path); read(&e.pathHash, sizeof(e.pathHash)); read(&e.fileId, sizeof(e.fileId)); read(&e.uncompressedSize, sizeof(e.uncompressedSize));
                read(&e.dataOfs, sizeof(e.dataOfs)); read(&e.chunkSize, sizeof(e.chunkSize)); read(&e.chunkCount, sizeof(e.chunkCount)); read(&e.flags, sizeof(e.flags));
                e.chunks.resize(e.chunkCount); for (u32 c = 0; c < e.chunkCount; c++) read(&e.chunks[c], sizeof(ChunkInfo));
                m_hashToIndex[e.pathHash] = m_entries.size(); m_entries.emplace_back(std::move(e));
            }
            std::fclose(fp);
        }

        struct FileInfo { std::string path; u64 size; bool encrypted; bool compressed; };
        std::vector<FileInfo> list() const {
            std::vector<FileInfo> out; out.reserve(m_entries.size());
            for (auto const& e : m_entries) out.push_back({ e.path, e.uncompressedSize, (bool)(e.flags & 1), (bool)(e.flags & 2) });
            return out;
        }

        bool contains(std::string_view virtualPath) const { return m_hashToIndex.contains(fnv1a64(virtualPath)); }

        std::vector<u8> readAll(std::string_view virtualPath) const {
            auto it = m_hashToIndex.find(fnv1a64(virtualPath)); ensure(it != m_hashToIndex.end(), "not found");
            return readByIndex(it->second);
        }

        void extractToFile(std::string_view virtualPath, const std::wstring& outPath) const {
            auto data = readAll(virtualPath);
            std::FILE* fp = nullptr; _wfopen_s(&fp, outPath.c_str(), L"wb"); ensure(fp, "open out");
            if (!data.empty()) fwrite(data.data(), 1, data.size(), fp); std::fclose(fp);
        }

    private:
        std::vector<u8> readByIndex(size_t idx) const {
            const Entry& e = m_entries[idx];
            std::FILE* fp = nullptr; _wfopen_s(&fp, m_path.wstring().c_str(), L"rb"); ensure(fp, "open pak");
            auto read = [&](void* p, size_t n) { if (std::fread(p, 1, n, fp) != n) fail("read pak chunk"); };
            auto seek = [&](u64 ofs) { _fseeki64(fp, ofs, SEEK_SET); };

            // prepare crypto
            std::array<u8, 16> ctr{}; std::optional<Crypto::Aes256Ctr> aes;
            if (e.flags & 1) { ensure(m_key.has_value(), "key required"); ctr = Crypto::Aes256Ctr::makeCtrIV(m_hdr.salt, e.fileId); aes.emplace(); aes->init(m_key.value(), ctr); }

            std::vector<u8> out; out.resize((size_t)e.uncompressedSize); size_t outOff = 0;
            for (u32 c = 0; c < e.chunkCount; c++) {
                const auto& ci = e.chunks[c];
                seek(ci.ofs); std::vector<u8> chunk; chunk.resize(ci.compSize); read(chunk.data(), chunk.size());
                if (e.flags & 1) { // decrypt in-place
                    aes->crypt_inplace(chunk.data(), chunk.size());
                    // advance CTR blocks
                    size_t blocks = (chunk.size() + 15) / 16; for (size_t b = 0; b < blocks; b++) { for (int k = 15; k >= 8; k--) { if (++ctr[k]) break; } }
                    // reinit with advanced ctr for next chunk
                    aes->init(m_key.value(), ctr);
                }
                std::vector<u8> plain;
                if (e.flags & 2) { // compressed
                    // If LZ4 integrated, use it; else pass-through (builder produced pass-through if not integrated)
                    Compression::LZ4Codec lz4; plain = lz4.decompress(chunk, ci.uncompSize);
                }
                else {
                    plain = std::move(chunk);
                    if (plain.size() != ci.uncompSize) plain.resize(ci.uncompSize); // trust header
                }
                std::memcpy(out.data() + outOff, plain.data(), plain.size()); outOff += plain.size();
            }
            std::fclose(fp); return out;
        }
    };

    // ------------------------------ Helpers -------------------------------------
    inline std::array<u8, 32> deriveKeyFromPassphrase(std::string_view pass, std::span<const u8> salt) {
        // 매우 단순한 KDF (데모용). 실제 서비스에서는 PBKDF2/Argon2 권장.
        std::vector<u8> buf; buf.reserve(pass.size() + salt.size());
        buf.insert(buf.end(), pass.begin(), pass.end()); buf.insert(buf.end(), salt.begin(), salt.end());
        return Crypto::sha256(buf);
    }

} // namespace Pak
