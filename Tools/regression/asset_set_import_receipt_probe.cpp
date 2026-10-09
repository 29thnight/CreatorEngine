// Source-only opt-in Windows regression harness. NOT built or run in this change.
// Compile this translation unit INSTEAD OF AssetSetBuild.cpp and link the
// AssetCooker native dependencies; including it exposes only local test seams.
#include "../AssetCooker/AssetSetBuild.cpp"
#include <objbase.h>

namespace
{
    void Require(bool condition, const std::string& reason)
    {
        if (!condition) throw std::runtime_error(reason);
    }

    void Replace(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        Require(static_cast<bool>(stream), "Cannot write isolated probe fixture");
    }

    void Replace(const std::filesystem::path& path, std::string_view text)
    {
        Replace(path, std::as_bytes(std::span(text.data(), text.size())));
    }

    constexpr unsigned char kRed[]{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
    constexpr unsigned char kBlue[]{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0xf8, 0xff, 0x1f, 0x00, 0x03, 0x02, 0x01, 0xff, 0xe6, 0x77, 0x0b, 0xae, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };

    void VerifyReceipts(const std::filesystem::path& work)
    {
        using namespace AssetCooking;
        const auto assets = work / "project" / "Assets";
        std::filesystem::create_directories(assets);
        Replace(assets / "a.png", std::as_bytes(std::span(kRed)));
        Replace(assets / "b.png", std::as_bytes(std::span(kRed)));
        Replace(assets / "a.png.meta", "guid: 11111111-1111-4111-8111-111111111111\n");
        Replace(assets / "b.png.meta", "guid: 33333333-3333-4333-8333-333333333333\n");
        const auto definition = work / "project" / "set.asset";
        const std::string source = R"(schemaVersion: 1
assetSetId: 22222222-2222-4222-8222-222222222222
revision: 1
inclusion: HardAndLoadable
target: { platform: win-x64, abi: receipt-probe-v1 }
settings: { textureEncoding: Source }
roots:
  - { assetId: 11111111-1111-4111-8111-111111111111, kind: Texture }
  - { assetId: 33333333-3333-4333-8333-333333333333, kind: Texture }
assets:
  - { assetId: 11111111-1111-4111-8111-111111111111, kind: Texture, source: a.png, dependencies: [] }
  - { assetId: 33333333-3333-4333-8333-333333333333, kind: Texture, source: b.png, dependencies: [] }
)";
        Replace(definition, source);
        AssetSetBuildRequest request{ assets, definition, {}, work / "cache", std::string(64u, 'a') };
        const auto build = [&](const char* name)
        {
            auto next = request;
            next.outputRoot = work / name;
            return BuildAssetSet(next);
        };
        const auto cold = build("cold");
        Require(cold.succeeded && cold.recookedImports == 2u && cold.reusedImports == 0u && cold.blobs == 1u,
            "Cold source import/deduplication: " + cold.failure);
        // Killed-process debris must never be interpreted as a committed receipt.
        std::filesystem::create_directories(request.artifactCache / ".asset-set-work-killed.incomplete");
        Replace(request.artifactCache / ".asset-set-work-killed.incomplete" / "import-receipt.tmp", "partial");
        const auto warm = build("warm");
        Require(warm.succeeded && warm.reusedImports == 2u && warm.recookedImports == 0u,
            "Warm build invoked a source producer: " + warm.failure);
        Require(Read(work / "cold" / "build-keys.txt") == Read(work / "warm" / "build-keys.txt"),
            "Warm build did not restore exact recipe metadata");
        auto revision = source;
        revision.replace(revision.find("revision: 1"), 11u, "revision: 2");
        Replace(definition, revision);
        const auto remanifest = build("revision");
        Require(remanifest.succeeded && remanifest.reusedImports == 2u && remanifest.recookedImports == 0u,
            "Manifest revision unnecessarily imported source");
        Replace(assets / "a.png.meta", "guid: 11111111-1111-4111-8111-111111111111\n# source setting changed\n");
        const auto sidecar = build("sidecar");
        Require(sidecar.succeeded && sidecar.reusedImports == 1u && sidecar.recookedImports == 1u,
            "One sidecar change did not isolate source import invalidation");
        Replace(assets / "a.png", std::as_bytes(std::span(kBlue)));
        const auto changed = build("changed");
        Require(changed.succeeded && changed.reusedImports == 1u && changed.recookedImports == 1u,
            "One source change did not isolate source import invalidation");
        request.toolFingerprint = std::string(64u, 'b');
        const auto tool = build("tool");
        Require(tool.succeeded && tool.reusedImports == 0u && tool.recookedImports == 2u,
            "Tool/compiler dependency change reused an import");
        {
            InputCapture held{ Canonical(assets), {} };
            held.Capture(assets / "a.png");
            const auto writer = CreateFileW((assets / "a.png").c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            Require(writer == INVALID_HANDLE_VALUE, "Capture allowed A-to-B-to-A writes");
            held.Verify();
        }
        {
            const auto writer = CreateFileW((assets / "a.png").c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            Require(writer != INVALID_HANDLE_VALUE, "Cannot open probe writer");
            bool rejected{};
            try { InputCapture held{ Canonical(assets), {} }; held.Capture(assets / "a.png"); }
            catch (const std::exception&) { rejected = true; }
            CloseHandle(writer);
            Require(rejected, "Capture accepted a preexisting writer");
        }
        std::filesystem::path blob;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(request.artifactCache / "Derived"))
            if (entry.is_regular_file()) { blob = entry.path(); break; }
        Require(!blob.empty(), "No immutable test blob");
        const auto original = Read(blob);
        auto damaged = original;
        damaged.back() ^= std::byte{ 1u };
        Replace(blob, damaged);
        const auto corrupt = build("corrupt");
        Require(!corrupt.succeeded && corrupt.failure.find("hash/size mismatch") != std::string::npos &&
            !std::filesystem::exists(work / "corrupt"), "Corrupt CAS was silently accepted/repaired");
        Replace(blob, original);
        std::filesystem::path receipt;
        {
            const auto parsed = ParseDefinition(Read(definition));
            const auto identity = *IncludeClosure(parsed).begin();
            const auto& authored = parsed.sources.at(identity);
            InputCapture capture{ Canonical(assets), {} };
            capture.Capture(assets / std::filesystem::u8path(authored.source));
            capture.Capture(assets / std::filesystem::u8path(authored.source + ".meta"), kMaxDefinitionBytes);
            const std::vector<ck::TypedAssetReference> selected{ authored.asset };
            const auto recipe = ImportRecipeKey(parsed, selected, capture, request.toolFingerprint);
            for (const auto& entry : std::filesystem::directory_iterator(request.artifactCache / "ImportReceipts" / recipe))
                if (entry.is_regular_file()) { receipt = entry.path(); break; }
        }
        Require(!receipt.empty(), "No committed current-recipe receipt");
        // Simulate a previously successful recipe with an obsolete external
        // input. Its valid receipt must miss without contaminating a new cook.
        std::filesystem::path obsoleteReceipt;
        {
            const auto parsed = ParseDefinition(Read(definition));
            const auto identity = *IncludeClosure(parsed).begin();
            const auto& authored = parsed.sources.at(identity);
            InputCapture capture{ Canonical(assets), {} };
            capture.Capture(assets / std::filesystem::u8path(authored.source));
            capture.Capture(assets / std::filesystem::u8path(authored.source + ".meta"), kMaxDefinitionBytes);
            const std::vector<ck::TypedAssetReference> selected{ authored.asset };
            const auto recipe = ImportRecipeKey(parsed, selected, capture, request.toolFingerprint);
            std::map<ck::AssetIdentity, PreparedArtifact> products;
            Require(ReuseImport(parsed, selected, capture, request.artifactCache, recipe, products), "Probe source receipt missing");
            Replace(assets / "obsolete.bin", "old external bytes");
            capture.Capture(assets / "obsolete.bin");
            auto candidate = MakeReceipt(parsed, selected, capture, request.artifactCache, recipe, products);
            obsoleteReceipt = candidate.path;
            WriteNew(candidate.path, candidate.bytes);
        }
        std::filesystem::remove(assets / "obsolete.bin");
        const auto receiptBytes = Read(receipt, kMaxReceiptBytes);
        auto damagedReceipt = receiptBytes;
        damagedReceipt.back() ^= std::byte{ 1u };
        Replace(receipt, damagedReceipt);
        const auto corruptReceipt = build("corrupt-receipt");
        Require(!corruptReceipt.succeeded && corruptReceipt.failure.find("filename/hash mismatch") != std::string::npos,
            "Corrupt receipt did not fail with its exact reason");
        Replace(receipt, receiptBytes);
        std::filesystem::remove(receipt);
        const auto missingReceipt = build("missing-receipt");
        Require(missingReceipt.succeeded && missingReceipt.reusedImports == 1u && missingReceipt.recookedImports == 1u,
            "Missing receipt did not fall back to one fresh source transaction");
        Require(Read(receipt, kMaxReceiptBytes) == receiptBytes,
            "Rejected candidate polluted the fresh receipt input inventory/key");
        const auto afterObsolete = build("after-obsolete");
        Require(afterObsolete.succeeded && afterObsolete.reusedImports == 2u && afterObsolete.recookedImports == 0u,
            "Obsolete missing dependency prevented the later exact hit");
        std::filesystem::remove(obsoleteReceipt);
        // Structural parser bounds are independent of which recipe is current.
        const std::vector<std::byte> truncated{ std::byte{ 1u } };
        bool rejected{};
        try { ReceiptReader reader{ truncated }; (void)reader.Number(); }
        catch (const std::exception&) { rejected = true; }
        Require(rejected, "Truncated receipt was accepted");
    }
}

int main()
{
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    struct ComScope final { ~ComScope() { CoUninitialize(); } } comScope;
    const auto work = std::filesystem::temp_directory_path() /
        ("asset-set-receipt-probe-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        Require(std::filesystem::create_directory(work), "Cannot reserve isolated probe directory");
        AssetCooking::DirectoryScope cleanup{ work };
        VerifyReceipts(work);
        std::cout << "asset_set_import_receipt_probe: pass\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "asset_set_import_receipt_probe: " << error.what() << '\n';
        return 1;
    }
}
