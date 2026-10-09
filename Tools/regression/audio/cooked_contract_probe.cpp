// Portable exact-source CEAC/CEMF tests retained from the 86f7efd baseline.
// No engine, audio device, Pak replacement, or altered production headers.
#include "Experiment/Cooked/CookedAudioClipSource.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace ck = experiment::cooked;
    namespace fs = std::filesystem;
    using Bytes = std::vector<std::byte>;
    std::size_t checks = 0;
    void Check(bool condition, const std::string& label) {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(label);
        }
    }
    void Put(Bytes& bytes, std::size_t at, std::uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i)
        {
            bytes.at(at + i) = std::byte(value >> (8 * i));
        }
    }
    experiment::AssetId Id(std::string_view text) {
        experiment::AssetId result;
        Check(experiment::TryParseCanonicalAssetId(text, result), "test GUID parses");
        return result;
    }
    class MemoryBytes final : public ck::ArtifactByteSource {
    public:
        Bytes bytes;
        std::string path;
        mutable std::size_t calls = 0, largestRead = 0;
        bool failSize = false, failRead = false;
        bool Size(std::string_view requested, std::uint64_t& out, std::string& why) const override {
            if (failSize || requested != path) { why = "fixture Size failure"; return false; }
            out = bytes.size(); return true;
        }
        bool ReadAt(std::string_view requested, std::uint64_t offset,
            std::span<std::byte> out, std::string& why) const override {
            ++calls; largestRead = std::max(largestRead, out.size());
            if (failRead || requested != path || offset > bytes.size() || out.size() > bytes.size() - offset) {
                why = "fixture ReadAt failure"; return false;
            }
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
            return true;
        }
    };
    struct Fixture {
        experiment::AssetId id = Id("01234567-89ab-4cde-8fab-0123456789ab");
        Bytes payload = Bytes(2 * 65536 + 113);
        Bytes artifact;
        ck::CookedAudioClipHeader header;
        ck::CookedAssetManifestEntry entry;
        Fixture() {
            for (std::size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = std::byte((i * 37 + i / 251) % 256);
            }
            header.codec = assets::AudioCodec::Wav;
            header.loadMode = ck::AudioLoadMode::Resident;
            header.spatialKind = ck::AudioSpatialKind::PointMono;
            header.channels = 1; header.sampleRate = 48000; header.frameCount = 4096;
            header.payloadBytes = payload.size();
            header.payloadSha256 = Hash::Sha256::Compute(payload.data(), payload.size());
            const auto h = ck::WriteAudioClipHeader(header);
            artifact.assign(h.begin(), h.end()); artifact.insert(artifact.end(), payload.begin(), payload.end());
            entry.assetId = id; entry.kind = ck::CookedAssetKind::AudioClip;
            entry.formatVersion = ck::kAudioClipArtifactVersion;
            entry.byteSize = artifact.size(); entry.artifactPath = ck::MakeDerivedAudioClipArtifactPath(id);
            entry.contentSha256 = Hash::Sha256::Compute(artifact.data(), artifact.size());
        }
        std::shared_ptr<MemoryBytes> Source() const {
            auto p = std::make_shared<MemoryBytes>(); p->bytes = artifact; p->path = entry.artifactPath; return p;
        }
    };
    void TestHeaders(const Fixture& f) {
        ck::CookedAudioClipHeader out;
        Check(ck::ReadAudioClipHeader(f.artifact, f.artifact.size(), out), "CEAC valid header");
        Check(out.payloadBytes == f.payload.size() && out.payloadSha256 == f.header.payloadSha256
            && out.sampleRate == 48000 && out.frameCount == 4096, "CEAC header roundtrip");
        for (std::size_t i = 0; i < ck::kAudioClipHeaderBytes; ++i)
        {
            Check(!ck::ReadAudioClipHeader(std::span(f.artifact).first(i), f.artifact.size(), out), "CEAC truncated header rejected");
        }
        Check(!ck::ReadAudioClipHeader(f.artifact, 71, out), "CEAC too-small declared extent rejected");
        Check(!ck::ReadAudioClipHeader(f.artifact, f.artifact.size() + 1, out), "CEAC extra extent rejected");
        Check(!ck::ReadAudioClipHeader(f.artifact, f.artifact.size() - 1, out), "CEAC short extent rejected");
        struct Mutation { std::size_t at; std::uint64_t value; unsigned size; const char* label; };
        const Mutation mutations[] = {
            {0,'X',1,"magic"}, {4,2,2,"version"}, {6,71,2,"header size"},
            {8,3,1,"codec"}, {9,3,1,"load mode"}, {10,2,1,"spatial kind"},
            {11,0,1,"zero channels"}, {11,3,1,"too many channels"}, {11,2,1,"spatial stereo"},
            {12,0,4,"zero rate"}, {16,0,8,"zero frames"}, {24,73,8,"offset"},
            {24,UINT64_MAX,8,"offset overflow"}, {32,0,8,"empty payload"},
            {32,UINT64_MAX,8,"payload overflow"}
        };
        for (const auto& m : mutations) {
            auto b = f.artifact; Put(b,m.at,m.value,m.size); out.frameCount = 777;
            Check(!ck::ReadAudioClipHeader(b,b.size(),out), std::string("CEAC rejects ") + m.label);
            Check(out.frameCount == 777, "CEAC failure leaves output unchanged");
        }
        auto zeroHash = f.artifact; std::fill(zeroHash.begin() + 40,zeroHash.begin() + 72,std::byte{});
        Check(!ck::ReadAudioClipHeader(zeroHash,zeroHash.size(),out), "CEAC zero payload digest rejected");
        for (unsigned codec = 0; codec != 3; ++codec)
        {
            for (unsigned mode = 0; mode != 3; ++mode)
            {
                auto b = f.artifact;
                Put(b, 8, codec, 1);
                Put(b, 9, mode, 1);
                Put(b, 10, 1, 1);
                Put(b, 11, 2, 1);
                Check(ck::ReadAudioClipHeader(b, b.size(), out), "CEAC supported codec/load/stereo metadata accepted");
            }
        }
        std::cout << "CEAC_HEADER_OK\n";
    }
    void TestMemory(const Fixture& f) {
        auto source=f.Source(); ck::CookedAudioClipSource clip; std::string why;
        Check(ck::OpenCookedAudioClipEntry(f.entry,source,clip,why), "CEAC opens with genuine hash verification: "+why);
        Check(source->calls == 4 && source->largestRead == 65536, "open verifies full payload with bounded 64KiB chunks");
        Check(clip.Id()==f.id && clip.PayloadSize()==f.payload.size(),"clip identity/extent preserved");
        Bytes actual(f.payload.size());
        Check(clip.ReadPayload(0,actual,why) && actual==f.payload,"CEAC full payload exact");
        std::array<std::byte,30> crossing{};
        Check(clip.ReadPayload(65530,crossing,why)
            && std::equal(crossing.begin(),crossing.end(),f.payload.begin()+65530),"CEAC crossing block read exact");
        std::array<std::byte,1> one{};
        Check(!clip.ReadPayload(clip.PayloadSize(),one,why),"CEAC nonempty EOF rejected");
        Check(clip.ReadPayload(clip.PayloadSize(),{},why),"CEAC empty EOF accepted");
        Check(!clip.ReadPayload(clip.PayloadSize()+1,{},why),"CEAC empty beyond EOF rejected");
        Check(!clip.ReadPayload(UINT64_MAX,one,why),"CEAC maximal offset rejected");
        Check(!ck::CookedAudioClipSource{}.ReadPayload(0,{},why),"unopened clip rejected");
        std::weak_ptr<MemoryBytes> lifetime=source; source.reset();
        Check(!lifetime.expired() && clip.ReadPayload(0,one,why),"source ownership survives caller release");

        const auto reject=[&](ck::CookedAssetManifestEntry entry, std::shared_ptr<MemoryBytes> bytes,const char* label){
            const auto prior=clip.Id();
            Check(!ck::OpenCookedAudioClipEntry(entry,std::move(bytes),clip,why),label);
            Check(!why.empty() && clip.Id()==prior && clip.PayloadSize()==f.payload.size(),"failed open preserves output and supplies reason");
        };
        auto changed=f.entry;changed.kind=ck::CookedAssetKind::Texture;reject(changed,f.Source(),"wrong manifest kind rejected");
        changed=f.entry;changed.formatVersion=2;reject(changed,f.Source(),"wrong manifest version rejected");
        changed=f.entry;changed.byteSize++;reject(changed,f.Source(),"manifest extent mismatch rejected");
        changed=f.entry;changed.contentSha256[0]^=1;reject(changed,f.Source(),"manifest digest mismatch rejected");
        changed=f.entry;changed.assetId=Id("11234567-89ab-4cde-8fab-0123456789ab");reject(changed,f.Source(),"GUID/path mismatch rejected");
        changed=f.entry;changed.artifactPath="Derived/Audio/../escape.ceac";reject(changed,f.Source(),"manifest path traversal rejected");
        reject(f.entry,nullptr,"null source rejected");
        auto bad=f.Source();bad->bytes.back()^=std::byte{1};reject(f.entry,bad,"payload tamper rejected");
        changed=f.entry;changed.contentSha256=Hash::Sha256::Compute(bad->bytes.data(),bad->bytes.size());
        reject(changed,bad,"payload tamper rejected even with updated outer digest");
        bad=f.Source();bad->bytes[40]^=std::byte{1};changed=f.entry;
        changed.contentSha256=Hash::Sha256::Compute(bad->bytes.data(),bad->bytes.size());
        reject(changed,bad,"inner digest tamper rejected even with updated outer digest");
        bad=f.Source();bad->bytes[0]=std::byte{'X'};reject(f.entry,bad,"invalid CEAC header rejected");
        bad=f.Source();bad->bytes.pop_back();reject(f.entry,bad,"truncated artifact rejected");
        bad=f.Source();bad->failSize=true;reject(f.entry,bad,"source Size error propagated");
        bad=f.Source();bad->failRead=true;reject(f.entry,bad,"source ReadAt error propagated");
        std::cout << "CEAC_BOUNDS_HASH_TAMPER_OK\n";
    }
    void Write(const fs::path& path,const Bytes& bytes) {
        fs::create_directories(path.parent_path()); std::ofstream stream(path,std::ios::binary|std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        Check(bool(stream),"fixture write succeeds");
    }
    void TestLoose(const Fixture& f,const fs::path& root) {
        const auto regularRoot=root/"regular";Write(regularRoot/fs::path(f.entry.artifactPath),f.artifact);
        auto loose=std::make_shared<ck::LooseArtifactByteSource>(regularRoot);std::string why;
        ck::CookedAudioClipSource clip;
        Check(ck::OpenCookedAudioClipEntry(f.entry,loose,clip,why),"loose CEAC open succeeds: "+why);
        Bytes actual(f.payload.size());
        Check(clip.ReadPayload(0,actual,why)&&actual==f.payload,"loose payload exact");
        const std::vector<std::string> paths={"","Derived/Audio/x.ceac/","Derived/Audio//x.ceac",
            "Derived/Audio/./x.ceac","Derived/Audio/../x.ceac","/Derived/Audio/x.ceac",
            "Derived/Audio/x.CEAC","Derived/Audio/x\\y.ceac","Derived/Audio/C:x.ceac",
            std::string("Derived/Audio/x\0.ceac",21)};
        for(const auto& path:paths){std::uint64_t n=0;
            Check(!ck::IsAudioArtifactVirtualPath(path),"invalid audio virtual path rejected");
            Check(!loose->Size(path,n,why),"loose invalid path rejected");
        }
        std::uint64_t n=0;
        Check(!loose->Size("Derived/Audio/missing.ceac",n,why),"missing loose artifact rejected");
        std::array<std::byte,1> one{};
        Check(loose->ReadAt(f.entry.artifactPath,f.artifact.size(),{},why),"loose empty EOF accepted");
        Check(!loose->ReadAt(f.entry.artifactPath,f.artifact.size(),one,why),"loose beyond EOF rejected");
        Check(!loose->ReadAt(f.entry.artifactPath,UINT64_MAX,one,why),"loose maximal offset rejected");
        const auto outside=root/"outside";fs::create_directories(outside);
        const auto escapeRoot=root/"escape";fs::create_directories(escapeRoot/"Derived");
        std::error_code ec;
        fs::create_directory_symlink(fs::absolute(outside),escapeRoot/"Derived/Audio",ec);
        Check(!ec,"symlink escape fixture created: "+ec.message());
        ck::LooseArtifactByteSource escape(escapeRoot);
        Check(!escape.Size(f.entry.artifactPath,n,why)&&why=="cooked artifact escapes root","symlink escape rejected");
        Check(!escape.ReadAt(f.entry.artifactPath,0,{},why),"symlink range escape rejected");
        std::atomic<bool> valid{true};std::array<std::thread,3> threads;
        for (auto& thread : threads)
        {
            thread = std::thread([&]
            {
                for (unsigned i = 0; i < 50; ++i)
                {
                    std::array<std::byte, 30> b{};
                    std::string error;
                    if (!clip.ReadPayload(65530, b, error) || !std::equal(b.begin(), b.end(), f.payload.begin() + 65530))
                    {
                        valid = false;
                    }
                }
            });
        }
        for(auto& t:threads) { t.join(); }
        Check(valid,"concurrent loose bounded reads exact");
#if defined(_WIN32)
        {
            std::fstream writer(regularRoot/fs::path(f.entry.artifactPath),
                std::ios::binary | std::ios::in | std::ios::out);
            Check(!writer.is_open(), "validated loose clip denies in-place writers");
        }
        clip = {};
#endif
        auto corrupt=f.artifact;corrupt.back()^=std::byte{1};Write(regularRoot/fs::path(f.entry.artifactPath),corrupt);
        ck::CookedAudioClipSource rejected;
        Check(!ck::OpenCookedAudioClipEntry(f.entry,loose,rejected,why),"mutated loose artifact rejected on open");
#if !defined(_WIN32)
        // The opened inode is pinned, but POSIX in-place mutation is still a
        // caller immutability violation. AudioBackend rehashes on payload load.
        Check(clip.ReadPayload(f.payload.size()-1,one,why)&&one[0]!=f.payload.back(),
            "post-open loose mutation remains observable; caller must enforce immutability");
#endif
        std::cout << "CEAC_LOOSE_PATH_CONCURRENCY_OK postOpenImmutability=caller-contract\n";
    }
    void TestLooseOwnership(const Fixture& f, const fs::path& root) {
        const auto pinRoot = root / "independent-pins";
        auto second = f.entry;
        second.assetId = Id("11234567-89ab-4cde-8fab-0123456789ab");
        second.artifactPath = ck::MakeDerivedAudioClipArtifactPath(second.assetId);
        const auto firstPath = pinRoot / fs::path(f.entry.artifactPath);
        const auto secondPath = pinRoot / fs::path(second.artifactPath);
        Write(firstPath, f.artifact);
        Write(secondPath, f.artifact);
        auto source = std::make_shared<ck::LooseArtifactByteSource>(pinRoot);
        std::weak_ptr<ck::LooseArtifactByteSource> weakMount = source;
        ck::CookedAudioClipSource first, sibling;
        std::string why;
        Check(ck::OpenCookedAudioClipEntry(f.entry, source, first, why), "first exact clip opened");
        Check(ck::OpenCookedAudioClipEntry(second, source, sibling, why), "sibling exact clip opened");
        auto firstCopy = first;
        Bytes actual(f.payload.size());
#if !defined(_WIN32)
        const auto retired = firstPath.string() + ".retired";
        fs::rename(firstPath, retired);
        fs::remove(retired);
        auto replaced = f.artifact;
        replaced.back() ^= std::byte{1};
        Write(firstPath, replaced);
        Check(first.ReadPayload(0, actual, why) && actual == f.payload,
            "legacy audio keeps old exact bytes after path replacement/unlink");
        ck::CookedAudioClipSource rejected;
        Check(!ck::OpenCookedAudioClipEntry(f.entry,
            std::make_shared<ck::LooseArtifactByteSource>(pinRoot), rejected, why),
            "a fresh source still rejects replacement content with the old manifest digest");
#endif
        first = {};
        Check(firstCopy.ReadPayload(0, actual, why) && actual == f.payload,
            "copy of exact audio source remains valid after original release");
        firstCopy = {};
#if defined(_WIN32)
        {
            std::fstream firstWriter(firstPath, std::ios::binary | std::ios::in | std::ios::out);
            std::fstream siblingWriter(secondPath, std::ios::binary | std::ios::in | std::ios::out);
            Check(firstWriter.is_open() && !siblingWriter.is_open(),
                "final audio reader releases only its own file while root and sibling remain");
        }
#else
        std::array<std::byte, 1> last{};
        Check(source->ReadAt(f.entry.artifactPath, f.artifact.size() - 1u, last, why)
            && last[0] != f.artifact.back(), "root weak lookup releases the old file after its last clip owner");
#endif
        source.reset();
        Check(weakMount.expired(), "a retained clip must not keep the entire loose mount source alive");
        Check(sibling.ReadPayload(0, actual, why) && actual == f.payload,
            "sibling audio still reads exact bytes after mount and other clip release");
        sibling = {};
#if defined(_WIN32)
        std::fstream writer(secondPath, std::ios::binary | std::ios::in | std::ios::out);
        Check(writer.is_open(), "final sibling audio reader releases its native handle");
#endif
        std::cout << "CEAC_INDEPENDENT_EXACT_PINS_OK\n";
    }
    void TestManifest(const Fixture& f) {
        ck::CookedAssetManifest m;m.entries={f.entry};m.sourceAssets={{f.id,"Audio/test.wav"}};
        auto encoded=ck::WriteAssetManifest(m);Check(encoded.Succeeded(),"CEMF writer accepts valid audio");
        ck::CookedAssetManifest decoded;std::vector<ck::AssetManifestIssue> issues;
        Check(ck::ReadAssetManifest(encoded.bytes,decoded,issues)&&issues.empty(),"CEMF reader roundtrip");
        const auto* found=decoded.Find(f.id);
        Check(found && found->artifactPath==f.entry.artifactPath && found->contentSha256==f.entry.contentSha256
            && found->byteSize==f.entry.byteSize,"CEMF audio identity/digest/extent roundtrip");
        Check(decoded.FindSource(f.id)&&decoded.FindSource(f.id)->sourcePath=="Audio/test.wav","CEMF source identity roundtrip");
        Check(decoded.Find({})==nullptr&&decoded.FindSource({})==nullptr,"CEMF missing GUID fails lookup");
        ck::CookedAudioClipSource clip;std::string why;
        Check(found && ck::OpenCookedAudioClipEntry(*found,f.Source(),clip,why),"CEMF entry opens actual CEAC payload");
        Check(ck::WriteAssetManifest(decoded).bytes==encoded.bytes,"CEMF byte-exact roundtrip");
        auto second=f.entry;second.assetId=Id("11234567-89ab-4cde-8fab-0123456789ab");
        second.artifactPath=ck::MakeDerivedAudioClipArtifactPath(second.assetId);
        m.entries.push_back(second);m.sourceAssets.push_back({second.assetId,"Audio/second.wav"});
        m.entries[1].dependencies.push_back(f.id);
        const auto ordered=ck::WriteAssetManifest(m);std::reverse(m.entries.begin(),m.entries.end());
        std::reverse(m.sourceAssets.begin(),m.sourceAssets.end());
        Check(ordered.Succeeded() && ck::WriteAssetManifest(m).bytes==ordered.bytes,"CEMF input-order determinism");
        auto rejectRead=[&](const Bytes& b,const char* label){
            auto out=decoded;issues.clear();Check(!ck::ReadAssetManifest(b,out,issues),label);
            Check(!issues.empty()&&ck::WriteAssetManifest(out).bytes==encoded.bytes,"CEMF reject reports issue and preserves output");
        };
        for (std::size_t size = 0; size < encoded.bytes.size(); ++size)
        {
            rejectRead(Bytes(encoded.bytes.begin(), encoded.bytes.begin() + size), "CEMF every truncated prefix rejected");
        }
        auto b=encoded.bytes;b.push_back(std::byte{});rejectRead(b,"CEMF trailing byte rejected");
        struct Mutation {std::size_t at;std::uint64_t value;unsigned size;const char* label;};
        const Mutation mutations[]={
            {0,0,4,"CEMF bad magic"},{4,99,2,"CEMF bad version"},{6,31,2,"CEMF header size"},
            {8,0,4,"CEMF no entries"},{8,UINT32_MAX,4,"CEMF malicious entry count"},
            {12,UINT32_MAX,4,"CEMF malicious dependency count"},{16,UINT32_MAX,4,"CEMF malicious string count"},
            {20,UINT32_MAX,4,"CEMF malicious source count"},{24,UINT32_MAX,4,"CEMF malicious source strings"},
            {28,1,4,"CEMF header reserved"},{32+16,255,1,"CEMF invalid kind"},
            {32+17,1,1,"CEMF entry reserved"},{32+20,2,4,"CEMF unsupported CEAC version"},
            {32+64,UINT32_MAX,4,"CEMF path offset"},{32+68,UINT32_MAX,4,"CEMF path length"},
            {32+72,UINT32_MAX,4,"CEMF dependency offset"},{32+76,UINT32_MAX,4,"CEMF dependency length"},
            {32+80+16,UINT32_MAX,4,"CEMF source path offset"},
            {32+80+20,UINT32_MAX,4,"CEMF source path length"}
        };
        for(const auto& bad:mutations){b=encoded.bytes;Put(b,bad.at,bad.value,bad.size);rejectRead(b,bad.label);}
        auto rejectsWrite=[&](const ck::CookedAssetManifest& bad,const char* label){
            const auto r=ck::WriteAssetManifest(bad);Check(!r.Succeeded()&&!r.issues.empty(),label);
        };
        auto bad=decoded;bad.entries.push_back(f.entry);rejectsWrite(bad,"CEMF duplicate GUID rejected");
        bad=decoded;bad.entries[0].dependencies={f.id};rejectsWrite(bad,"CEMF self dependency rejected");
        bad=decoded;bad.entries[0].dependencies={second.assetId};rejectsWrite(bad,"CEMF unresolved dependency rejected");
        bad=decoded;bad.entries[0].artifactPath="Derived/../escape.ceac";rejectsWrite(bad,"CEMF traversal rejected");
        bad=decoded;bad.entries[0].contentSha256={};rejectsWrite(bad,"CEMF absent digest rejected");
        bad=decoded;bad.entries[0].assetId={};rejectsWrite(bad,"CEMF nil GUID rejected");
        bad=decoded;bad.sourceAssets.push_back(bad.sourceAssets[0]);rejectsWrite(bad,"CEMF duplicate source rejected");
        bad=decoded;bad.sourceAssets[0].sourcePath="../test.wav";rejectsWrite(bad,"CEMF source traversal rejected");
        issues.clear();Check(ck::VerifyArtifact(f.entry,f.artifact.size(),f.entry.contentSha256,issues),"CEMF artifact stamp matches");
        issues.clear();Check(!ck::VerifyArtifact(f.entry,f.artifact.size()+1,f.entry.contentSha256,issues),"CEMF extent stamp rejects");
        auto digest=f.entry.contentSha256;digest[0]^=1;issues.clear();
        Check(!ck::VerifyArtifact(f.entry,f.artifact.size(),digest,issues),"CEMF digest stamp rejects");
        std::cout << "CEMF_ROUNDTRIP_DETERMINISM_BOUNDS_OK\n";
    }
}

int main(int argc,char** argv){try{
    if(argc!=2) { throw std::runtime_error("usage: cooked_contract_harness <new-fixture-directory>"); }
    Check(!fs::exists(argv[1]),"fixture directory must be new; production files are never reused");
    fs::create_directories(argv[1]);
    const std::array<std::uint8_t,32> abc={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Check(Hash::Sha256::Compute("abc",3)==abc,"production SHA256 known-answer vector");
    Fixture f;TestHeaders(f);TestMemory(f);TestLoose(f,fs::path(argv[1]));TestLooseOwnership(f,fs::path(argv[1]));TestManifest(f);
    std::cout<<"COOKED_CONTRACT_OK checks="<<checks<<" pak=not-tested device=not-used\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"COOKED_CONTRACT_FAILED after="<<checks<<" reason="<<e.what()<<'\n';return 1;}}
