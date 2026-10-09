#include "RuntimeBootstrap.h"
#include "../../Utility_Framework/ContentAbi.h"
#include "../Assets/AssetIdentityProfile.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    namespace cooked = experiment::cooked;
    constexpr std::size_t MaxBytes = 4u * 1024u * 1024u;
    constexpr std::size_t MaxDocuments = 16384u;
    constexpr std::size_t MaxReferences = 65536u;

    std::string HashText(const cooked::Sha256Digest& digest)
    {
        constexpr char hex[] = "0123456789abcdef";
        std::string text;
        for (auto byte : digest)
        {
            text.push_back(hex[byte >> 4u]);
            text.push_back(hex[byte & 15u]);
        }
        return text;
    }

    cooked::Sha256Digest ParseHash(std::string_view text)
    {
        if (text.size() != 64u || !std::ranges::all_of(text, [](char c)
            { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw std::runtime_error("Invalid bootstrap SHA256.");
        cooked::Sha256Digest digest{};
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (std::size_t i = 0; i != digest.size(); ++i)
            digest[i] = static_cast<std::uint8_t>((digit(text[2u * i]) << 4u) | digit(text[2u * i + 1u]));
        return digest;
    }

    experiment::AssetId ParseId(const std::string& text)
    {
        experiment::AssetId id;
        if (!experiment::TryParseCanonicalAssetId(text, id)
            && !assets::TryParseCanonicalUuidV8(text, id.value))
            throw std::runtime_error("Invalid bootstrap asset UUID.");
        return id;
    }

    bool ExternalKind(cooked::CookedAssetKind kind)
    {
        using enum cooked::CookedAssetKind;
        return kind == Model || kind == Mesh || kind == Skeleton || kind == AnimationClip
            || kind == Texture || kind == Material || kind == MaterialProgram || kind == ShaderMeta;
    }

    std::vector<std::byte> ReadMetadata(const std::filesystem::path& path, std::size_t limit)
    {
        own::shared_owner<const cooked::ArtifactByteSource> source =
            own::make_shared<const cooked::LooseArtifactByteSource>(path.parent_path());
        std::string error;
        std::uint64_t size{};
        const auto name = path.filename().generic_string();
        if (!cooked::CaptureArtifactSource(source, name, error) || !source->Size(name, size, error)
            || size == 0u || size > limit)
            throw std::runtime_error("Bootstrap metadata is missing/oversized: " + error);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if (!source->ReadAt(name, 0u, bytes, error)) throw std::runtime_error(error);
        return bytes;
    }
}

namespace AssetDepot
{
    bool WriteRuntimeBootstrapReceipt(const RuntimeBootstrapReceipt& receipt,
        std::string& text, std::string& failure)
    {
        failure.clear();
        try
        {
            auto hashes = receipt.assetSetHashes;
            auto documents = receipt.documents;
            std::ranges::sort(hashes);
            std::ranges::sort(documents, {}, &RuntimeBootstrapDocument::assetId);
            if (hashes.empty() || hashes.size() > 64u || documents.empty() || documents.size() > MaxDocuments
                || std::adjacent_find(hashes.begin(), hashes.end()) != hashes.end())
                throw std::runtime_error("Bootstrap requires 1..64 distinct sets and 1..16384 documents.");
            std::ostringstream output;
            output << "CEBR1\nwin-x64\n" << CreatorContentAbi::Token << "\nlegacy "
                << HashText(receipt.legacyManifestSha256) << '\n';
            for (const auto& hash : hashes)
            {
                (void)ParseHash(hash);
                output << "set " << hash << '\n';
            }
            std::set<experiment::AssetId> identities;
            std::size_t references{};
            for (auto& document : documents)
            {
                const auto id = Uuid::ToString(document.assetId.value);
                if (ParseId(id) != document.assetId || !experiment::IsAssetIdV4(document.assetId)
                    || !identities.insert(document.assetId).second)
                    throw std::runtime_error("Invalid or repeated bootstrap document UUID.");
                output << "document " << id << ' ' << HashText(document.contentSha256) << '\n';
                std::ranges::sort(document.references);
                if (std::adjacent_find(document.references.begin(), document.references.end()) != document.references.end())
                    throw std::runtime_error("Repeated bootstrap reference.");
                std::map<experiment::AssetId, cooked::CookedAssetKind> kinds;
                for (const auto& reference : document.references)
                {
                    if (++references > MaxReferences || !ExternalKind(reference.kind)
                        || reference.key.subassetId != experiment::AssetId{}
                        || ParseId(Uuid::ToString(reference.key.assetId.value)) != reference.key.assetId
                        || !kinds.emplace(reference.key.assetId, reference.kind).second)
                        throw std::runtime_error("Invalid, conflicting or excessive bootstrap references.");
                    output << "reference " << id << ' ' << static_cast<unsigned>(reference.kind)
                        << ' ' << Uuid::ToString(reference.key.assetId.value) << '\n';
                }
            }
            auto candidate = output.str();
            if (candidate.size() > MaxBytes) throw std::runtime_error("Bootstrap receipt exceeds 4 MiB.");
            text = std::move(candidate);
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }

    bool ReadRuntimeBootstrapReceipt(std::string_view text,
        RuntimeBootstrapReceipt& receipt, std::string& failure)
    {
        failure.clear();
        try
        {
            if (text.empty() || text.size() > MaxBytes || text.back() != '\n'
                || text.find('\r') != text.npos || text.find('\0') != text.npos)
                throw std::runtime_error("Malformed or oversized bootstrap receipt.");
            std::istringstream input{std::string(text)};
            const auto line = [&input]()
            {
                std::string value;
                if (!std::getline(input, value)) throw std::runtime_error("Truncated bootstrap receipt.");
                return value;
            };
            if (line() != "CEBR1" || line() != "win-x64" || line() != CreatorContentAbi::Token)
                throw std::runtime_error("Unsupported bootstrap format/platform/installed content ABI.");
            RuntimeBootstrapReceipt result;
            const auto legacy = line();
            if (!legacy.starts_with("legacy ")) throw std::runtime_error("Missing bootstrap legacy hash.");
            result.legacyManifestSha256 = ParseHash(std::string_view(legacy).substr(7u));
            std::size_t referenceCount{};
            while (input.peek() != std::char_traits<char>::eof())
            {
                const auto record = line();
                std::istringstream fields(record);
                std::string operation, id, hash, kind, target, extra;
                fields >> operation;
                if (operation == "set" && result.documents.empty())
                {
                    if (!(fields >> hash) || fields >> extra || result.assetSetHashes.size() >= 64u)
                        throw std::runtime_error("Invalid bootstrap set record.");
                    (void)ParseHash(hash);
                    result.assetSetHashes.push_back(hash);
                }
                else if (operation == "document")
                {
                    if (!(fields >> id >> hash) || fields >> extra || result.documents.size() >= MaxDocuments)
                        throw std::runtime_error("Invalid bootstrap document record.");
                    result.documents.push_back({ParseId(id), ParseHash(hash), {}});
                }
                else if (operation == "reference")
                {
                    if (!(fields >> id >> kind >> target) || fields >> extra || result.documents.empty()
                        || ++referenceCount > MaxReferences || ParseId(id) != result.documents.back().assetId)
                        throw std::runtime_error("Invalid bootstrap reference record.");
                    unsigned number{};
                    const auto parsed = std::from_chars(kind.data(), kind.data() + kind.size(), number);
                    if (parsed.ec != std::errc{} || parsed.ptr != kind.data() + kind.size() || number > 255u)
                        throw std::runtime_error("Invalid bootstrap reference kind.");
                    result.documents.back().references.push_back({{ParseId(target), {}}, static_cast<cooked::CookedAssetKind>(number)});
                }
                else throw std::runtime_error("Unknown bootstrap record or invalid record order.");
            }
            std::string canonical;
            if (!WriteRuntimeBootstrapReceipt(result, canonical, failure) || canonical != text)
                throw std::runtime_error("Noncanonical bootstrap receipt: " + failure);
            receipt = std::move(result);
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }

    bool ValidateRuntimeBootstrap(const std::filesystem::path& assetRoot,
        const std::vector<std::string>& assetSetHashes, const cooked::CookedAssetCatalog& candidate,
        std::string& failure)
    {
        failure.clear();
        try
        {
            const auto path = assetRoot / "Derived/bootstrap-asset-references.cebr";
            std::error_code error;
            if (!std::filesystem::exists(path, error))
            {
                if (error) throw std::runtime_error("Cannot inspect bootstrap receipt.");
                return true;
            }
            const auto bytes = ReadMetadata(path, MaxBytes);
            RuntimeBootstrapReceipt receipt;
            if (!ReadRuntimeBootstrapReceipt({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, receipt, failure))
                return false;
            auto hashes = assetSetHashes;
            std::ranges::sort(hashes);
            if (hashes != receipt.assetSetHashes)
                throw std::runtime_error("Bootstrap AssetSet group changed; rebuild the document bootstrap.");
            const auto legacyBytes = ReadMetadata(assetRoot / "Derived/asset-manifest.cemf", 128u * 1024u * 1024u);
            cooked::Sha256Digest digest;
            cooked::CookedAssetManifest legacy;
            std::vector<cooked::AssetManifestIssue> issues;
            if (!cooked::ComputeSha256(legacyBytes, digest, failure) || digest != receipt.legacyManifestSha256
                || !cooked::ReadAssetManifest(legacyBytes, legacy, issues))
                throw std::runtime_error("Bootstrap legacy manifest changed or is invalid.");
            std::size_t documentCount{};
            for (const auto& entry : legacy.entries)
            {
                using enum cooked::CookedAssetKind;
                if (entry.kind != Scene && entry.kind != Prefab && entry.kind != AudioClip
                    && entry.kind != SoundGraph && entry.kind != SoundPreset && entry.kind != CollisionGeometry)
                    throw std::runtime_error("Bootstrap contains a non-document legacy payload.");
                cooked::ResolvedAssetEntry overlap;
                if (candidate.Find({{entry.assetId, {}}, entry.kind}, overlap) != cooked::AssetLookupStatus::NotMounted)
                {
                    throw std::runtime_error("Bootstrap legacy identity conflicts with a mounted AssetSet.");
                }
                if (entry.kind == Scene || entry.kind == Prefab) ++documentCount;
            }
            if (documentCount != receipt.documents.size()) throw std::runtime_error("Bootstrap document inventory differs.");
            for (const auto& document : receipt.documents)
            {
                const auto* entry = legacy.Find(document.assetId);
                if (!entry || (entry->kind != cooked::CookedAssetKind::Scene && entry->kind != cooked::CookedAssetKind::Prefab)
                    || entry->contentSha256 != document.contentSha256)
                    throw std::runtime_error("Bootstrap document identity/hash differs.");
                for (const auto& reference : document.references)
                {
                    cooked::ResolvedAssetEntry resolved;
                    if (legacy.Find(reference.key.assetId)
                        || candidate.Find(reference, resolved) != cooked::AssetLookupStatus::Found)
                        throw std::runtime_error("Bootstrap typed external reference is missing, ambiguous or has the wrong kind: "
                            + Uuid::ToString(reference.key.assetId.value));
                }
            }
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }
}
