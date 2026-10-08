#include "AssetSetBuild.h"
#include "../../Engine/Utility_Framework/ArtifactStoreGuard.h"

#include "AuthoringParsedDocument.h"
#include "AuthoringCookedDocument.h"
#include "Experiment/Cooked/ModelCookIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookSupport.h"
#include "Experiment/Cooked/TextureCookProducer.h"
#include "Experiment/Cooked/ModelAssetSetProducer.h"
#include "Experiment/Cooked/ShaderMetaCookProducer.h"
#include "Experiment/Cooked/CookedShaderMeta.h"
#include "Experiment/Cooked/MaterialAssetSetProducer.h"
#include "Assets/AssetIdentityProfile.h"
#include "Texture.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace AssetCooking
{
    namespace
    {
        namespace ck = experiment::cooked;
        constexpr std::uint64_t kMaxSourceBytes = 512ull * 1024ull * 1024ull;
        constexpr std::size_t kMaxDefinitionBytes = 16u * 1024u * 1024u;
        constexpr std::size_t kMaxAssets = 65536u;
        constexpr std::size_t kMaxEdges = 262144u;
        // Bump for producer/decoder behavior or normalization changes.
        constexpr std::string_view kTextureImporterVersion = "texture-source-image-v1";
        constexpr std::string_view kModelImporterVersion = "model-source-subassets-v3";
        constexpr std::string_view kShaderMetaImporterVersion = "shadermeta-source-document-v1";
        constexpr std::string_view kMaterialImporterVersion = "lattice-material-source-document-v1";
        constexpr std::string_view kMaterialProgramImporterVersion = "lattice-source-verified-program-v1";
        constexpr std::string_view kBuildVersion = "asset-set-build-v3";
        constexpr std::uint32_t kTextureSourceImage = 1u;

        struct AssetSource final
        {
            ck::TypedAssetReference asset{};
            std::string source{};
            std::string verifiedProgram{};
            std::vector<ck::AssetDependency> dependencies{};
        };

        struct AssetSet final
        {
            experiment::AssetId assetSetId{};
            std::uint64_t revision{};
            std::string targetPlatform{};
            std::string targetAbi{};
            std::vector<ck::TypedAssetReference> roots{};
            std::map<ck::AssetIdentity, AssetSource> sources{};
        };

        [[noreturn]] void Fail(const std::string& message)
        {
            throw std::runtime_error(message);
        }

        std::string Label(const ck::AssetIdentity& key)
        {
            return Uuid::ToString(key.assetId.value);
        }

        void RequireMap(Authoring::ReadNode node, std::initializer_list<std::string_view> keys,
            const std::string& context)
        {
            if (!node.IsMap())
            {
                Fail(context + ": expected a map");
            }
            std::set<std::string> seen;
            for (const auto field : node.Map())
            {
                const auto key = field.key.AsString();
                if (std::find(keys.begin(), keys.end(), key) == keys.end() || !seen.insert(key).second)
                {
                    Fail(context + ": unknown or duplicate field '" + key + "'");
                }
            }
        }

        std::string Text(Authoring::ReadNode node, const char* field)
        {
            const auto value = node[field];
            if (!value.IsScalar() || value.Scalar().empty())
            {
                Fail(std::string("missing scalar field: ") + field);
            }
            return value.AsString();
        }

        std::uint64_t Unsigned(Authoring::ReadNode node, const char* field)
        {
            const auto text = Text(node, field);
            std::uint64_t value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0)
            {
                Fail(std::string(field) + ": expected a nonzero unsigned integer");
            }
            return value;
        }

        bool Token(std::string_view value)
        {
            return !value.empty() && value.size() <= 256u && std::ranges::all_of(value, [](char ch)
            {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '+' || ch == '-';
            });
        }

        experiment::AssetId Id(Authoring::ReadNode node, const char* field)
        {
            experiment::AssetId id{};
            if (!experiment::TryParseCanonicalAssetId(Text(node, field), id))
            {
                Fail(std::string(field) + ": this source build requires a canonical UUIDv4");
            }
            return id;
        }

        ck::TypedAssetReference Reference(Authoring::ReadNode node)
        {
            const auto name = Text(node, "kind");
            ck::CookedAssetKind kind{};
            if (name == "Texture" || name == "Material" || name == "MaterialProgram")
            {
                experiment::AssetId id;
                const auto text = Text(node, "assetId");
                if (!experiment::TryParseCanonicalAssetId(text, id) &&
                    !assets::TryParseCanonicalUuidV8(text, id.value))
                {
                    Fail(name + ": assetId must be a canonical UUIDv4 or already-authored UUIDv8");
                }
                kind = name == "Texture" ? ck::CookedAssetKind::Texture :
                    name == "Material" ? ck::CookedAssetKind::Material : ck::CookedAssetKind::MaterialProgram;
                return { { id, {} }, kind };
            }
            if (name == "ShaderMeta")
            {
                return { { Id(node, "assetId"), {} }, ck::CookedAssetKind::ShaderMeta };
            }
            if (name == "Model")
            {
                kind = ck::CookedAssetKind::Model;
            }
            else if (name == "Mesh")
            {
                kind = ck::CookedAssetKind::Mesh;
            }
            else if (name == "Skeleton")
            {
                kind = ck::CookedAssetKind::Skeleton;
            }
            else if (name == "AnimationClip")
            {
                kind = ck::CookedAssetKind::AnimationClip;
            }
            else
            {
                Fail("Unsupported AssetSet source kind: " + name +
                    "; supported: Texture, ShaderMeta, Material, MaterialProgram, Model, Mesh, Skeleton, AnimationClip");
            }
            const auto text = Text(node, "assetId");
            Uuid::Uuid16 parsed{};
            if (!Uuid::TryParse(text, parsed) || Uuid::ToString(parsed) != text ||
                (parsed.data[6] & 0xf0u) != 0x80u || (parsed.data[8] & 0xc0u) != 0x80u)
            {
                Fail(name + ": assetId must be an already-authored canonical UUIDv8");
            }
            return { { experiment::AssetId{ parsed }, {} }, kind };
        }

        bool RelativePath(std::string_view text)
        {
            if (text.empty() || text.size() > 4096u || text.front() == '/' || text.back() == '/' ||
                text.find_first_of("\\:") != std::string_view::npos)
            {
                return false;
            }
            std::size_t offset{};
            while (offset < text.size())
            {
                const auto end = text.find('/', offset);
                const auto part = text.substr(offset,
                    end == std::string_view::npos ? text.size() - offset : end - offset);
                if (part.empty() || part == "." || part == ".." || part.back() == ' ' || part.back() == '.' ||
                    std::ranges::any_of(part, [](unsigned char ch) { return ch < 32u || ch == 127u; }))
                {
                    return false;
                }
                offset = end == std::string_view::npos ? text.size() : end + 1u;
            }
            return true;
        }

        // Junctions are reparse points too; weakly_canonical alone is insufficient.
        void NoReparse(const std::filesystem::path& path)
        {
            for (auto probe = std::filesystem::absolute(path); !probe.empty(); probe = probe.parent_path())
            {
                const auto attributes = GetFileAttributesW(probe.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    Fail("AssetSet path crosses a reparse point: " + probe.string());
                }
                if (probe == probe.parent_path())
                {
                    break;
                }
            }
        }

        std::filesystem::path Canonical(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                Fail("AssetSet path is empty");
            }
            NoReparse(path);
            return std::filesystem::weakly_canonical(path);
        }

        void Disjoint(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            if (left == right || ck::IsContainedPath(left, right) || ck::IsContainedPath(right, left))
            {
                Fail("AssetSet input/output/cache trees overlap: " + left.string() + " / " + right.string());
            }
        }

        std::vector<std::byte> Read(const std::filesystem::path& path, std::uint64_t limit = kMaxSourceBytes)
        {
            NoReparse(path);
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit)
            {
                Fail("Missing or oversized AssetSet input: " + path.string());
            }
            std::vector<std::byte> bytes;
            if (!ck::ReadBinaryFile(path, bytes) || bytes.empty() || bytes.size() > limit)
            {
                Fail("Cannot read nonempty AssetSet input: " + path.string());
            }
            return bytes;
        }

        std::string Hash(std::span<const std::byte> bytes)
        {
            ck::Sha256Digest digest{};
            std::string failure;
            if (!ck::ComputeSha256(bytes, digest, failure))
            {
                Fail("AssetSet SHA256: " + failure);
            }
            constexpr char hex[] = "0123456789abcdef";
            std::string text;
            text.reserve(64u);
            for (const auto byte : digest)
            {
                text += hex[byte >> 4u];
                text += hex[byte & 15u];
            }
            return text;
        }

        std::string Hash(std::string_view text)
        {
            return Hash(std::as_bytes(std::span(text.data(), text.size())));
        }

        // Build-local input pins, not a cache lock. A source writer already
        // holding incompatible access makes capture fail; later writes/deletes
        // are excluded until the exact bytes have been consumed and checked.
        struct CapturedFile final
        {
            HANDLE handle{ INVALID_HANDLE_VALUE };
            BY_HANDLE_FILE_INFORMATION identity{};
            FILE_BASIC_INFO basic{};
            std::vector<std::byte> bytes{};
            std::string digest{};

            CapturedFile() = default;
            CapturedFile(const CapturedFile&) = delete;
            CapturedFile& operator=(const CapturedFile&) = delete;
            ~CapturedFile()
            {
                if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            }
        };

        bool SameFile(const BY_HANDLE_FILE_INFORMATION& left, const BY_HANDLE_FILE_INFORMATION& right)
        {
            return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
                left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow &&
                left.nFileSizeHigh == right.nFileSizeHigh && left.nFileSizeLow == right.nFileSizeLow;
        }

        std::vector<std::byte> ReadCaptured(HANDLE handle, std::uint64_t limit,
            const std::filesystem::path& path)
        {
            LARGE_INTEGER size{};
            LARGE_INTEGER start{};
            if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
                static_cast<std::uint64_t>(size.QuadPart) > limit ||
                !SetFilePointerEx(handle, start, nullptr, FILE_BEGIN))
            {
                Fail("Cannot size/seek bounded captured input: " + path.string());
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
            std::size_t offset{};
            while (offset != bytes.size())
            {
                const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024u * 1024u));
                DWORD received{};
                if (!ReadFile(handle, bytes.data() + offset, count, &received, nullptr) || received != count)
                {
                    Fail("Captured input changed or could not be read: " + path.string());
                }
                offset += received;
            }
            return bytes;
        }

        struct InputCapture final
        {
            std::filesystem::path root{};
            std::filesystem::path epoch{};
            std::map<std::filesystem::path, CapturedFile> files{};
            std::uint64_t totalBytes{};

            const CapturedFile& Capture(const std::filesystem::path& path,
                std::uint64_t limit = kMaxSourceBytes, bool definition = false)
            {
                const auto canonical = Canonical(path);
                if (!definition && !ck::IsContainedPath(root, canonical) && canonical != epoch)
                {
                    Fail("Captured import input escapes Assets/identity header: " + canonical.string());
                }
                if (const auto found = files.find(canonical); found != files.end())
                {
                    if (found->second.bytes.size() > limit) Fail("Captured input exceeds role byte limit: " + canonical.string());
                    return found->second;
                }
                if (files.size() >= 4096u) Fail("Import capture exceeds 4096 input files");
                auto& captured = files.try_emplace(canonical).first->second;
                captured.handle = CreateFileW(canonical.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (captured.handle == INVALID_HANDLE_VALUE ||
                    !GetFileInformationByHandle(captured.handle, &captured.identity) ||
                    !GetFileInformationByHandleEx(captured.handle, FileBasicInfo, &captured.basic, sizeof(captured.basic)) ||
                    (captured.identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
                {
                    Fail("Cannot capture stable regular input (active writer, missing file or unsafe path; Win32 " +
                        std::to_string(GetLastError()) + "): " + canonical.string());
                }
                captured.bytes = ReadCaptured(captured.handle, limit, canonical);
                if (captured.bytes.size() > 1024ull * 1024ull * 1024ull - totalBytes)
                {
                    Fail("Import capture exceeds 1 GiB input byte budget");
                }
                totalBytes += captured.bytes.size();
                captured.digest = Hash(captured.bytes);
                Verify(canonical, captured);
                return captured;
            }

            void Verify(const std::filesystem::path& path, const CapturedFile& captured) const
            {
                BY_HANDLE_FILE_INFORMATION current{};
                FILE_BASIC_INFO basic{};
                if (Canonical(path) != path ||
                    !GetFileInformationByHandle(captured.handle, &current) || !SameFile(captured.identity, current) ||
                    !GetFileInformationByHandleEx(captured.handle, FileBasicInfo, &basic, sizeof(basic)) ||
                    basic.ChangeTime.QuadPart != captured.basic.ChangeTime.QuadPart ||
                    basic.LastWriteTime.QuadPart != captured.basic.LastWriteTime.QuadPart ||
                    ReadCaptured(captured.handle, kMaxSourceBytes, path) != captured.bytes)
                {
                    Fail("Source changed during import capture: " + path.string());
                }
                // A parent directory rename must not rebind this held file to a
                // different current pathname, even if the replacement bytes match.
                CapturedFile named;
                named.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (named.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(named.handle, &current) ||
                    !SameFile(captured.identity, current))
                {
                    Fail("Captured source pathname changed during import: " + path.string());
                }
            }

            void Verify() const
            {
                for (const auto& [path, captured] : files) Verify(path, captured);
            }
        };

        AssetSet ParseDefinition(std::span<const std::byte> bytes)
        {
            std::string failure;
            const auto document = Authoring::ParsedDocument::ParseText(
                std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), failure);
            if (!document)
            {
                Fail("AssetSet source definition: " + failure);
            }
            const auto root = document.Root();
            RequireMap(root, { "schemaVersion", "assetSetId", "revision", "inclusion", "target",
                "settings", "roots", "assets" }, "AssetSet");
            if (Unsigned(root, "schemaVersion") != 1u || Text(root, "inclusion") != "HardAndLoadable")
            {
                Fail("AssetSet requires source schemaVersion 1 and inclusion HardAndLoadable; "
                    "v2 conversion is unsupported");
            }
            AssetSet result;
            result.assetSetId = Id(root, "assetSetId");
            result.revision = Unsigned(root, "revision");
            const auto target = root["target"];
            RequireMap(target, { "platform", "abi" }, "target");
            result.targetPlatform = Text(target, "platform");
            result.targetAbi = Text(target, "abi");
            if (result.targetPlatform != "win-x64" || !Token(result.targetAbi))
            {
                Fail("AssetSet target requires win-x64 and an explicit compatible ABI token");
            }
            RequireMap(root["settings"], { "textureEncoding" }, "settings");
            if (Text(root["settings"], "textureEncoding") != "Source")
            {
                Fail("Only textureEncoding Source is implemented; no transcoding or mip generation is implied");
            }
            const auto roots = root["roots"];
            const auto sources = root["assets"];
            if (!roots.IsSequence() || roots.Size() == 0 || roots.Size() > kMaxAssets ||
                !sources.IsSequence() || sources.Size() == 0 || sources.Size() > kMaxAssets)
            {
                Fail("AssetSet roots/assets must be nonempty sequences of at most 65536 entries");
            }
            std::set<ck::TypedAssetReference> distinctRoots;
            for (const auto node : roots)
            {
                RequireMap(node, { "assetId", "kind" }, "root");
                const auto reference = Reference(node);
                if (!distinctRoots.insert(reference).second)
                {
                    Fail("Duplicate root: " + Label(reference.key));
                }
                result.roots.push_back(reference);
            }
            std::size_t edgeCount{};
            for (const auto node : sources)
            {
                RequireMap(node, { "assetId", "kind", "source", "verifiedProgram", "dependencies" }, "asset source");
                AssetSource source;
                source.asset = Reference(node);
                source.source = Text(node, "source");
                if (!RelativePath(source.source))
                {
                    Fail("Asset source must be normalized relative to Assets: " + source.source);
                }
                if (source.asset.kind == ck::CookedAssetKind::MaterialProgram)
                {
                    source.verifiedProgram = Text(node, "verifiedProgram");
                    if (!RelativePath(source.verifiedProgram))
                    {
                        Fail("verifiedProgram must be normalized relative to Assets: " + source.source);
                    }
                }
                else if (node["verifiedProgram"])
                {
                    Fail("verifiedProgram is only valid for MaterialProgram sources: " + source.source);
                }
                const auto dependencies = node["dependencies"];
                if (!dependencies.IsSequence())
                {
                    Fail("Every source must explicitly declare dependencies, including [] for leaves: " +
                        source.source);
                }
                std::set<ck::AssetIdentity> distinctDependencies;
                for (const auto edge : dependencies)
                {
                    if (++edgeCount > kMaxEdges)
                    {
                        Fail("AssetSet source exceeds 262144 dependency edges");
                    }
                    RequireMap(edge, { "assetId", "kind", "dependency", "scope" }, "dependency");
                    ck::AssetDependency dependency;
                    dependency.target = Reference(edge);
                    const auto kind = Text(edge, "dependency");
                    if (kind != "Hard" && kind != "Loadable")
                    {
                        Fail("Dependency must explicitly be Hard or Loadable: " + source.source);
                    }
                    dependency.kind = kind == "Hard"
                        ? ck::AssetDependencyKind::Hard : ck::AssetDependencyKind::Loadable;
                    const auto scope = edge["scope"] ? Text(edge, "scope") : "Internal";
                    if (scope != "Internal" && scope != "External")
                    {
                        Fail("Dependency scope must be Internal or External: " + source.source);
                    }
                    dependency.scope = scope == "Internal"
                        ? ck::AssetDependencyScope::Internal : ck::AssetDependencyScope::External;
                    if (!distinctDependencies.insert(dependency.target.key).second)
                    {
                        Fail("Duplicate dependency from " + source.source + " to " + Label(dependency.target.key));
                    }
                    source.dependencies.push_back(dependency);
                }
                std::ranges::sort(source.dependencies);
                if (!result.sources.emplace(source.asset.key, source).second)
                {
                    Fail("Duplicate source asset ID: " + Label(source.asset.key));
                }
            }
            return result;
        }

        std::set<ck::AssetIdentity> IncludeClosure(const AssetSet& definition)
        {
            std::set<ck::AssetIdentity> included;
            std::vector<ck::TypedAssetReference> pending = definition.roots;
            while (!pending.empty())
            {
                const auto reference = pending.back();
                pending.pop_back();
                const auto found = definition.sources.find(reference.key);
                if (found == definition.sources.end() || found->second.asset.kind != reference.kind)
                {
                    Fail("Missing or mismatched typed source in build closure: " + Label(reference.key));
                }
                if (included.insert(reference.key).second)
                {
                    for (const auto& edge : found->second.dependencies)
                    {
                        // Both relations enter this build only for Internal
                        // scope. External keeps its explicit typed declaration
                        // for atomic mount-union/runtime validation.
                        if (edge.scope == ck::AssetDependencyScope::Internal)
                        {
                            pending.push_back(edge.target);
                        }
                    }
                }
            }
            return included;
        }

        // Iterative Tarjan traversal keeps source-authored chains off the C++ call stack.
        // Loadable backreferences do not participate in ownership SCCs.
        void RejectHardCycles(const AssetSet& definition, const std::set<ck::AssetIdentity>& included)
        {
            struct State final
            {
                std::size_t index{};
                std::size_t low{};
                bool onStack{};
            };
            struct Frame final
            {
                ck::AssetIdentity key{};
                std::size_t next{};
            };
            std::map<ck::AssetIdentity, State> states;
            std::vector<ck::AssetIdentity> componentStack;
            std::vector<Frame> frames;
            std::size_t nextIndex = 1u;
            const auto enter = [&](const ck::AssetIdentity& key)
            {
                states.emplace(key, State{ nextIndex, nextIndex, true });
                ++nextIndex;
                componentStack.push_back(key);
                frames.push_back({ key, 0u });
            };
            for (const auto& start : included)
            {
                if (states.contains(start))
                {
                    continue;
                }
                enter(start);
                while (!frames.empty())
                {
                    auto& frame = frames.back();
                    auto& state = states.at(frame.key);
                    const auto& edges = definition.sources.at(frame.key).dependencies;
                    if (frame.next < edges.size())
                    {
                        const auto edge = edges[frame.next++];
                        if (edge.kind != ck::AssetDependencyKind::Hard ||
                            edge.scope != ck::AssetDependencyScope::Internal)
                        {
                            continue;
                        }
                        const auto found = states.find(edge.target.key);
                        if (found == states.end())
                        {
                            enter(edge.target.key);
                        }
                        else if (found->second.onStack)
                        {
                            state.low = std::min(state.low, found->second.index);
                        }
                        continue;
                    }
                    const auto key = frame.key;
                    if (state.low == state.index)
                    {
                        std::vector<ck::AssetIdentity> component;
                        do
                        {
                            const auto member = componentStack.back();
                            componentStack.pop_back();
                            states.at(member).onStack = false;
                            component.push_back(member);
                        } while (component.back() != key);
                        const bool selfCycle = std::ranges::any_of(edges, [&](const auto& edge)
                        {
                            return edge.kind == ck::AssetDependencyKind::Hard &&
                                edge.scope == ck::AssetDependencyScope::Internal && edge.target.key == key;
                        });
                        if (component.size() > 1u || selfCycle)
                        {
                            std::string diagnostic = "Hard ownership cycle (SCC); source paths:";
                            std::ranges::sort(component);
                            for (const auto& member : component)
                            {
                                const auto& source = definition.sources.at(member);
                                diagnostic += "\n  " + source.source + " [" + Label(member) + "]";
                                for (const auto& edge : source.dependencies)
                                {
                                    if (edge.kind == ck::AssetDependencyKind::Hard &&
                                        edge.scope == ck::AssetDependencyScope::Internal &&
                                        std::ranges::binary_search(component, edge.target.key))
                                    {
                                        diagnostic += " -> " + definition.sources.at(edge.target.key).source;
                                    }
                                }
                            }
                            Fail(diagnostic);
                        }
                    }
                    frames.pop_back();
                    if (!frames.empty())
                    {
                        auto& parent = states.at(frames.back().key);
                        parent.low = std::min(parent.low, state.low);
                    }
                }
            }
        }

        void ValidateTexture(std::span<const std::byte> bytes, const std::string& extension)
        {
            if (ck::SniffTextureExtension(bytes) != extension)
            {
                Fail("Texture container signature does not match its declared source extension " + extension);
            }
            TextureImage image;
            std::string failure;
            if (!Texture::DecodeToRgba8(bytes, image, failure))
            {
                Fail("Texture format validation failed: " + failure);
            }
        }

        void ValidatePayload(std::span<const std::byte> bytes, const ck::TypedAssetReference& asset,
            std::span<const ck::AssetDependency> dependencies, const std::string& extension)
        {
            std::string failure;
            bool valid{};
            switch (asset.kind)
            {
            case ck::CookedAssetKind::Texture:
                ValidateTexture(bytes, extension);
                return;
            case ck::CookedAssetKind::ShaderMeta:
            {
                ShaderMeta value;
                valid = extension == ".shadermeta" &&
                    ck::ReadShaderMetaArtifact(bytes, asset.key.assetId, value, failure);
                break;
            }
            case ck::CookedAssetKind::Material:
            {
                material_graph::InstanceDocument value;
                valid = extension == ".asset" &&
                    ck::ReadMaterialAssetSetDocument(bytes, asset, dependencies, value, failure);
                break;
            }
            case ck::CookedAssetKind::MaterialProgram:
            {
                material_graph::CookedProgram value;
                valid = extension == ".lxmaterial" &&
                    ck::ReadMaterialProgramAssetSetArtifact(bytes, asset, dependencies, {}, value, failure);
                break;
            }
            case ck::CookedAssetKind::Model:
            {
                ck::ModelDescriptorArtifact value;
                valid = extension == ".cemd" && ck::ReadModelDescriptorArtifact(bytes, value, failure);
                break;
            }
            case ck::CookedAssetKind::Mesh:
            {
                ck::ModelGeometryArtifact value;
                std::vector<std::string> warnings;
                valid = extension == ".cege" && ck::ReadModelGeometryArtifact(bytes, value, failure, &warnings);
                if (valid && !warnings.empty())
                {
                    failure = warnings.front();
                    valid = false; // 게시/CAS 검증에서는 derived fallback으로 손상을 숨기지 않는다.
                }
                break;
            }
            case ck::CookedAssetKind::Skeleton:
            {
                ck::SkeletonArtifact value;
                valid = extension == ".cesl" && ck::ReadSkeletonArtifact(bytes, value, failure);
                break;
            }
            case ck::CookedAssetKind::AnimationClip:
            {
                ck::AnimationClipArtifact value;
                valid = extension == ".cean" && ck::ReadAnimationClipArtifact(bytes, value, failure);
                break;
            }
            default:
                Fail("No exact AssetSet payload codec registered for source kind");
            }
            if (!valid)
            {
                Fail("Typed AssetSet payload/extension validation failed: " + failure);
            }
        }

        void VerifyBytes(const std::filesystem::path& path, std::uint64_t size,
            const std::string& digest, const ck::TypedAssetReference& asset,
            std::span<const ck::AssetDependency> dependencies, const std::string& extension)
        {
            const auto bytes = Read(path);
            if (bytes.size() != size || Hash(bytes) != digest)
            {
                Fail("Immutable AssetSet blob hash/size mismatch: " + path.string());
            }
            ValidatePayload(bytes, asset, dependencies, extension);
        }

        void WriteNew(const std::filesystem::path& path, std::span<const std::byte> bytes)
        {
            NoReparse(path);
            if (std::filesystem::exists(path))
            {
                Fail("Refusing to replace immutable AssetSet output: " + path.string());
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::out);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            stream.close();
            if (!stream)
            {
                Fail("Cannot write AssetSet output: " + path.string());
            }
        }

        void WriteNew(const std::filesystem::path& path, std::string_view text)
        {
            WriteNew(path, std::as_bytes(std::span(text.data(), text.size())));
        }

        struct DirectoryScope final
        {
            std::filesystem::path path{};
            ~DirectoryScope()
            {
                if (!path.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(path, ignored);
                }
            }
        };

        // File existence is not lock ownership. Share-mode zero grants exclusion
        // only while this process holds the handle; Windows closes it on forced
        // termination too, when C++ destructors cannot run.
        class AssetSetCacheLock final
        {
        public:
            explicit AssetSetCacheLock(const std::filesystem::path& path)
            {
                NoReparse(path);
                m_handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0u, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (m_handle == INVALID_HANDLE_VALUE)
                {
                    const auto error = GetLastError();
                    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
                    {
                        Fail("Artifact cache is in use by another live cook: " + path.string());
                    }
                    Fail("Cannot open artifact cache guard (Win32 " + std::to_string(error) +
                        "): " + path.string());
                }
                BY_HANDLE_FILE_INFORMATION information{};
                if (!GetFileInformationByHandle(m_handle, &information) ||
                    (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
                {
                    CloseHandle(m_handle);
                    m_handle = INVALID_HANDLE_VALUE;
                    Fail("Artifact cache guard is not a regular local file: " + path.string());
                }
            }

            ~AssetSetCacheLock()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(m_handle);
                }
            }

            AssetSetCacheLock(const AssetSetCacheLock&) = delete;
            AssetSetCacheLock& operator=(const AssetSetCacheLock&) = delete;
            AssetSetCacheLock(AssetSetCacheLock&&) = delete;
            AssetSetCacheLock& operator=(AssetSetCacheLock&&) = delete;

        private:
            HANDLE m_handle{ INVALID_HANDLE_VALUE };
        };

        std::string Compatibility(const AssetSet& definition, ck::CookedAssetKind kind,
            std::uint32_t representation, std::uint32_t schema)
        {
            return "kind=" + std::to_string(static_cast<unsigned>(kind)) +
                "\nrepresentation=" + std::to_string(representation) + "\nschema=" + std::to_string(schema) +
                "\nplatform=" + definition.targetPlatform + "\nabi=" + definition.targetAbi + "\n";
        }

        struct PreparedArtifact final
        {
            std::vector<std::byte> bytes{};
            std::string extension{};
            std::string inputDigest{};
            std::string importKey{};
            std::string importer{};
            std::uint32_t representation{};
            std::uint32_t schema{};
        };

        constexpr std::string_view kImportReceiptVersion = "asset-set-import-receipt-v1";
        constexpr std::size_t kMaxReceiptBytes = 64u * 1024u * 1024u;

        // Length framing, including every string, makes source-authored path
        // tokens and dependency inventories unambiguous without delimiter rules.
        void ReceiptNumber(std::vector<std::byte>& bytes, std::uint64_t value)
        {
            for (unsigned index = 0; index < 8u; ++index)
                bytes.push_back(static_cast<std::byte>((value >> (8u * index)) & 0xffu));
        }

        void ReceiptText(std::vector<std::byte>& bytes, std::string_view value)
        {
            ReceiptNumber(bytes, value.size());
            const auto span = std::as_bytes(std::span(value.data(), value.size()));
            bytes.insert(bytes.end(), span.begin(), span.end());
        }

        struct ReceiptReader final
        {
            std::span<const std::byte> bytes;
            std::size_t offset{};
            std::uint64_t Number()
            {
                if (bytes.size() - offset < 8u) Fail("Truncated import receipt integer");
                std::uint64_t value{};
                for (unsigned index = 0; index < 8u; ++index)
                    value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[offset++])) << (8u * index);
                return value;
            }
            std::string Text(std::size_t limit = 4096u)
            {
                const auto size = Number();
                if (size > limit || size > bytes.size() - offset) Fail("Truncated/oversized import receipt string");
                std::string value(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::size_t>(size));
                offset += static_cast<std::size_t>(size);
                return value;
            }
        };

        bool DigestText(std::string_view value)
        {
            return value.size() == 64u && std::ranges::all_of(value, [](char ch)
            {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            });
        }

        PreparedArtifact ExpectedFormat(const AssetSource& source)
        {
            PreparedArtifact value;
            const bool model = assets::IsUuidV8(source.asset.key.assetId.value);
            value.importer = model ? kModelImporterVersion : kTextureImporterVersion;
            switch (source.asset.kind)
            {
            case ck::CookedAssetKind::Texture:
                value.representation = kTextureSourceImage; value.schema = ck::kTextureArtifactVersion;
                if (!model)
                {
                    value.extension = std::filesystem::u8path(source.source).extension().string();
                    std::ranges::transform(value.extension, value.extension.begin(), [](unsigned char ch)
                    { return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch); });
                    if (!ck::IsSupportedTextureExtension(value.extension)) Fail("Unsupported receipt texture source extension");
                }
                break;
            case ck::CookedAssetKind::ShaderMeta:
                value.importer = kShaderMetaImporterVersion;
                value.representation = ck::kShaderMetaDocumentRepresentation; value.schema = ck::kShaderMetaDocumentVersion;
                value.extension = ".shadermeta"; break;
            case ck::CookedAssetKind::Material:
                if (!model) value.importer = kMaterialImporterVersion;
                value.representation = ck::kMaterialDocumentRepresentation; value.schema = ck::kMaterialArtifactVersion;
                value.extension = ".asset"; break;
            case ck::CookedAssetKind::MaterialProgram:
                value.importer = kMaterialProgramImporterVersion;
                value.representation = ck::kMaterialProgramRepresentation; value.schema = ck::kMaterialProgramArtifactVersion;
                value.extension = ".lxmaterial"; break;
            case ck::CookedAssetKind::Model:
                value.representation = ck::kModelDescriptorRepresentation; value.schema = ck::kModelDescriptorVersion;
                value.extension = ".cemd"; break;
            case ck::CookedAssetKind::Mesh:
                value.representation = ck::kModelGeometryRepresentation; value.schema = ck::kModelGeometryArtifactVersion;
                value.extension = ".cege"; break;
            case ck::CookedAssetKind::Skeleton:
                value.representation = ck::kSkeletonRepresentation; value.schema = ck::kSkeletonArtifactVersion;
                value.extension = ".cesl"; break;
            case ck::CookedAssetKind::AnimationClip:
                value.representation = ck::kAnimationClipRepresentation; value.schema = ck::kAnimationClipArtifactVersion;
                value.extension = ".cean"; break;
            default: Fail("Unsupported import receipt kind");
            }
            return value;
        }

        std::string InputPath(const InputCapture& capture, const std::filesystem::path& path)
        {
            const auto relative = path.lexically_relative(capture.root.parent_path()).generic_u8string();
            const std::string text(relative.begin(), relative.end());
            if (!RelativePath(text)) Fail("Import input cannot be represented project-relatively: " + path.string());
            return text;
        }

        void AppendInput(std::vector<std::byte>& bytes, const InputCapture& capture,
            const std::filesystem::path& path, const CapturedFile& input)
        {
            ReceiptText(bytes, InputPath(capture, path));
            ReceiptNumber(bytes, input.bytes.size());
            ReceiptText(bytes, input.digest);
        }

        std::string ImportRecipeKey(const AssetSet& definition,
            std::span<const ck::TypedAssetReference> selections, const InputCapture& initial,
            std::string_view toolFingerprint)
        {
            std::vector<std::byte> bytes;
            ReceiptText(bytes, kImportReceiptVersion);
            ReceiptText(bytes, kBuildVersion);
            ReceiptText(bytes, toolFingerprint); // verified binary/compiler/native dependency closure
            ReceiptText(bytes, definition.targetPlatform);
            ReceiptText(bytes, definition.targetAbi);
            ReceiptText(bytes, "textureEncoding=Source;materialBudget=default-v1;resolver=strict-captured-v1");
            ReceiptNumber(bytes, ck::kAssetSetManifestVersion);
            ReceiptNumber(bytes, selections.size());
            for (const auto& selection : selections)
            {
                const auto& source = definition.sources.at(selection.key);
                const auto expected = ExpectedFormat(source);
                ReceiptText(bytes, Label(selection.key));
                ReceiptNumber(bytes, static_cast<unsigned>(selection.kind));
                ReceiptText(bytes, source.source);
                ReceiptText(bytes, source.verifiedProgram);
                ReceiptText(bytes, expected.importer);
                ReceiptNumber(bytes, expected.representation);
                ReceiptNumber(bytes, expected.schema);
                ReceiptText(bytes, expected.extension);
                ReceiptNumber(bytes, source.dependencies.size());
                for (const auto& edge : source.dependencies)
                {
                    ReceiptText(bytes, Label(edge.target.key));
                    ReceiptNumber(bytes, static_cast<unsigned>(edge.target.kind));
                    ReceiptNumber(bytes, static_cast<unsigned>(edge.kind));
                    ReceiptNumber(bytes, static_cast<unsigned>(edge.scope));
                }
            }
            ReceiptNumber(bytes, initial.files.size());
            for (const auto& [path, input] : initial.files) AppendInput(bytes, initial, path, input);
            return Hash(bytes);
        }

        std::string CapturedImportKey(const InputCapture& capture, std::string_view recipeKey)
        {
            std::vector<std::byte> bytes;
            ReceiptText(bytes, kImportReceiptVersion);
            ReceiptText(bytes, recipeKey);
            ReceiptNumber(bytes, capture.files.size());
            for (const auto& [path, input] : capture.files) AppendInput(bytes, capture, path, input);
            return Hash(bytes);
        }

        std::string BlobPath(const AssetSet& definition, const AssetSource& authored,
            const PreparedArtifact& value, const std::string& digest)
        {
            return "Derived/AssetBlobs/" + Hash(Compatibility(definition, authored.asset.kind,
                value.representation, value.schema)) + "/" + digest + value.extension;
        }

        struct PendingReceipt final
        {
            std::filesystem::path path{};
            std::vector<std::byte> bytes{};
        };

        bool ReuseImport(const AssetSet& definition, std::span<const ck::TypedAssetReference> selections,
            InputCapture& capture, const std::filesystem::path& cache, const std::string& recipeKey,
            std::map<ck::AssetIdentity, PreparedArtifact>& products)
        {
            const auto directory = cache / "ImportReceipts" / recipeKey;
            NoReparse(directory);
            if (!std::filesystem::exists(directory)) return false;
            if (!std::filesystem::is_directory(directory)) Fail("Import receipt index is not a directory: " + directory.string());
            std::vector<std::filesystem::path> candidates;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (candidates.size() >= 256u) Fail("Import receipt index exceeds 256 bounded candidates: " + directory.string());
                candidates.push_back(entry.path());
            }
            std::ranges::sort(candidates);
            std::set<std::filesystem::path> required;
            for (const auto& [path, input] : capture.files) required.insert(path);
            for (const auto& path : candidates)
            {
                try
                {
                    const auto bytes = Read(path, kMaxReceiptBytes);
                    if (path.extension() != ".receipt" || path.stem().string() != Hash(bytes))
                        Fail("Import receipt immutable filename/hash mismatch");
                    ReceiptReader reader{ bytes };
                    if (reader.Text() != kImportReceiptVersion || reader.Text() != recipeKey)
                        Fail("Incompatible import receipt version/recipe");
                    const auto inputs = reader.Number();
                    if (inputs == 0u || inputs > 4096u) Fail("Invalid import receipt input count");
                    bool matches = true;
                    // A rejected candidate must not poison a later hit or a
                    // fresh producer with its obsolete dependency inventory.
                    InputCapture candidateCapture{ capture.root, capture.epoch };
                    std::set<std::filesystem::path> inventoried;
                    for (std::uint64_t index = 0; index < inputs; ++index)
                    {
                        const auto relative = reader.Text();
                        const auto size = reader.Number();
                        const auto digest = reader.Text(64u);
                        if (!RelativePath(relative) || size > kMaxSourceBytes || !DigestText(digest))
                            Fail("Invalid import receipt input path/size/hash");
                        const auto inputPath = Canonical(capture.root.parent_path() / std::filesystem::u8path(relative));
                        if (InputPath(capture, inputPath) != relative || !inventoried.insert(inputPath).second)
                            Fail("Noncanonical or duplicate import receipt input");
                        if (!ck::IsContainedPath(capture.root, inputPath) && inputPath != capture.epoch)
                            Fail("Import receipt input escapes Assets/identity header");
                        if (!std::filesystem::is_regular_file(inputPath))
                        {
                            matches = false; // Current source producer decides the actual closure.
                            continue;
                        }
                        const auto& input = candidateCapture.Capture(inputPath);
                        matches = matches && size == input.bytes.size() && digest == input.digest;
                    }
                    if (!std::ranges::includes(inventoried, required)) Fail("Import receipt omits mandatory source inputs");
                    if (reader.Number() != selections.size()) Fail("Import receipt selected product count mismatch");
                    std::map<ck::AssetIdentity, PreparedArtifact> restored;
                    for (const auto& selection : selections)
                    {
                        const auto& authored = definition.sources.at(selection.key);
                        auto value = ExpectedFormat(authored);
                        if (reader.Text() != Label(selection.key) || reader.Number() != static_cast<unsigned>(selection.kind) ||
                            reader.Text() != value.importer || reader.Number() != value.representation || reader.Number() != value.schema)
                            Fail("Import receipt typed identity/importer/schema mismatch");
                        const auto extension = reader.Text(32u);
                        if ((!value.extension.empty() && value.extension != extension) ||
                            (value.extension.empty() && !ck::IsSupportedTextureExtension(extension)))
                            Fail("Import receipt representation extension mismatch");
                        value.extension = extension;
                        value.inputDigest = reader.Text(64u);
                        const auto digest = reader.Text(64u);
                        const auto size = reader.Number();
                        if (!DigestText(value.inputDigest) || !DigestText(digest) || size == 0 || size > kMaxSourceBytes)
                            Fail("Invalid import receipt artifact digest/size");
                        if (matches)
                        {
                            const auto blob = cache / std::filesystem::u8path(BlobPath(definition, authored, value, digest));
                            value.bytes = Read(blob);
                            if (value.bytes.size() != size || Hash(value.bytes) != digest)
                                Fail("Import receipt CAS blob hash/size mismatch: " + blob.string());
                            ValidatePayload(value.bytes, authored.asset, authored.dependencies, value.extension);
                            restored.emplace(selection.key, std::move(value));
                        }
                    }
                    if (reader.offset != bytes.size()) Fail("Trailing import receipt bytes");
                    capture.Verify();
                    candidateCapture.Verify();
                    if (matches)
                    {
                        const auto importKey = CapturedImportKey(candidateCapture, recipeKey);
                        for (auto& [identity, value] : restored) value.importKey = importKey;
                        products.insert(std::make_move_iterator(restored.begin()), std::make_move_iterator(restored.end()));
                        return true; // No source importer, converter or source parser invoked.
                    }
                }
                catch (const std::exception& error)
                {
                    Fail("Import receipt rejected: " + path.string() + ": " + error.what());
                }
            }
            return false;
        }

        PendingReceipt MakeReceipt(const AssetSet& definition, std::span<const ck::TypedAssetReference> selections,
            const InputCapture& capture, const std::filesystem::path& cache, const std::string& recipeKey,
            std::map<ck::AssetIdentity, PreparedArtifact>& products)
        {
            capture.Verify();
            PendingReceipt receipt;
            ReceiptText(receipt.bytes, kImportReceiptVersion);
            ReceiptText(receipt.bytes, recipeKey);
            ReceiptNumber(receipt.bytes, capture.files.size());
            for (const auto& [path, input] : capture.files) AppendInput(receipt.bytes, capture, path, input);
            ReceiptNumber(receipt.bytes, selections.size());
            const auto importKey = CapturedImportKey(capture, recipeKey);
            for (const auto& selection : selections)
            {
                const auto& authored = definition.sources.at(selection.key);
                auto& value = products.at(selection.key);
                value.importKey = importKey;
                const auto expected = ExpectedFormat(authored);
                if (value.importer != expected.importer || value.representation != expected.representation ||
                    value.schema != expected.schema || (!expected.extension.empty() && value.extension != expected.extension))
                    Fail("Produced recipe format disagrees with import receipt schema");
                ReceiptText(receipt.bytes, Label(selection.key));
                ReceiptNumber(receipt.bytes, static_cast<unsigned>(selection.kind));
                ReceiptText(receipt.bytes, value.importer);
                ReceiptNumber(receipt.bytes, value.representation);
                ReceiptNumber(receipt.bytes, value.schema);
                ReceiptText(receipt.bytes, value.extension);
                ReceiptText(receipt.bytes, value.inputDigest);
                ReceiptText(receipt.bytes, Hash(value.bytes));
                ReceiptNumber(receipt.bytes, value.bytes.size());
            }
            if (receipt.bytes.size() > kMaxReceiptBytes) Fail("Import receipt exceeds 64 MiB record budget");
            receipt.path = cache / "ImportReceipts" / recipeKey / (Hash(receipt.bytes) + ".receipt");
            return receipt;
        }

        bool MatchesRecipeDependencies(std::span<const ck::AssetDependency> authored,
            std::span<const ck::AssetDependency> produced)
        {
            // The producer determines semantic references and eager/lazy intent.
            // The definition chooses which set supplies each exact typed target.
            // Both lists are canonical, sorted and unique by logical identity.
            return authored.size() == produced.size() &&
                std::equal(authored.begin(), authored.end(), produced.begin(),
                    [](const auto& declared, const auto& required)
                    {
                        return declared.target == required.target && declared.kind == required.kind;
                    });
        }

        bool IsExternalDependency(const AssetSource& source, const ck::TypedAssetReference& target)
        {
            return std::ranges::any_of(source.dependencies, [&](const auto& edge)
            {
                return edge.target == target && edge.scope == ck::AssetDependencyScope::External;
            });
        }

        PreparedArtifact PrepareShaderMeta(const AssetSource& authored,
            const std::filesystem::path& assetRoot, InputCapture& capture)
        {
            if (!authored.dependencies.empty())
            {
                Fail("Authored ShaderMeta descriptors require dependencies: []; generated graph metadata "
                    "belongs to its verified MaterialProgram: " + authored.source);
            }
            const auto source = Canonical(assetRoot / std::filesystem::u8path(authored.source));
            if (!ck::IsContainedPath(assetRoot, source))
            {
                Fail("Shader metadata source escapes asset root: " + authored.source);
            }
            auto metaPath = source;
            metaPath += ".meta";
            const auto& sourceInput = capture.Capture(source, ck::kShaderMetaDocumentMaxBytes).bytes;
            const auto& metaInput = capture.Capture(metaPath, kMaxDefinitionBytes).bytes;
            ShaderMeta sourceMetadata;
            std::string failure;
            if (!ShaderMetaLoader::Parse(
                { reinterpret_cast<const char*>(sourceInput.data()), sourceInput.size() }, source,
                FileGuid{ authored.asset.key.assetId.value }, sourceMetadata, failure))
            {
                Fail("Shader metadata authoring validation failed: " + authored.source + ": " + failure);
            }
            if (sourceMetadata.generatedMaterial)
            {
                Fail("Generated ShaderMeta must be cooked as part of its verified MaterialProgram: " +
                    authored.source);
            }
            // These inputs are validated by the canonical authoring producer.
            // Snapshot them for a coherent source import key; HLSL is not a new
            // runtime dependency and is never opened by descriptor acquisition.
            const auto shaderSource = Canonical(sourceMetadata.ResolveSource(source));
            if (!ck::IsContainedPath(assetRoot, shaderSource))
            {
                Fail("Shader source escapes asset root: " + authored.source);
            }
            auto shaderMetaPath = shaderSource;
            shaderMetaPath += ".meta";
            const auto& shaderInput = capture.Capture(shaderSource, 16u * 1024u * 1024u).bytes;
            const auto& shaderMetaInput = capture.Capture(shaderMetaPath, kMaxDefinitionBytes).bytes;
            auto cooked = ck::BuildShaderMetaCookProduct({ source, assetRoot });
            if (!cooked.Succeeded())
            {
                failure = "Shader metadata source cook failed: " + authored.source;
                for (const auto& issue : cooked.issues)
                {
                    failure += "\n  " + issue.context + ": " + issue.message;
                }
                Fail(failure);
            }
            auto& product = *cooked.product;
            // Bind the emitted CEDO and both identities to the captured bytes,
            // not merely before/after file observations (which can miss A-B-A).
            const auto capturedDocument = Authoring::ParsedDocument::ParseText(
                { reinterpret_cast<const char*>(sourceInput.data()), sourceInput.size() }, failure);
            std::vector<std::byte> capturedArtifact;
            std::vector<ck::ModelIdentityIssue> identityIssues;
            experiment::AssetId capturedId;
            experiment::AssetId capturedShaderId;
            if (!capturedDocument ||
                !Authoring::EncodeCookedDocument(capturedDocument.Root(), capturedArtifact, failure) ||
                !ck::ReadAssetIdFromMeta(
                    { reinterpret_cast<const char*>(metaInput.data()), metaInput.size() }, capturedId, identityIssues) ||
                !ck::ReadAssetIdFromMeta(
                    { reinterpret_cast<const char*>(shaderMetaInput.data()), shaderMetaInput.size() },
                    capturedShaderId, identityIssues) ||
                capturedId != authored.asset.key.assetId || capturedShaderId != product.sourceShaderAssetId ||
                capturedArtifact != product.artifactBytes)
            {
                Fail("Shader metadata product differs from its captured source/sidecar bytes: " + authored.source);
            }
            if (product.shaderMetaAssetId != authored.asset.key.assetId ||
                Read(source, ck::kShaderMetaDocumentMaxBytes) != sourceInput ||
                Read(metaPath, kMaxDefinitionBytes) != metaInput ||
                Read(shaderSource, 16u * 1024u * 1024u) != shaderInput ||
                Read(shaderMetaPath, kMaxDefinitionBytes) != shaderMetaInput)
            {
                Fail("Shader metadata inputs changed during cook or definition differs from sidecar: " +
                    authored.source);
            }
            ShaderMeta readback;
            if (!ck::ReadShaderMetaArtifact(product.artifactBytes, authored.asset.key.assetId,
                readback, failure))
            {
                Fail("Source-free shader metadata readback failed: " + failure);
            }
            PreparedArtifact value;
            value.bytes = std::move(product.artifactBytes);
            value.extension = ".shadermeta";
            value.inputDigest = Hash("source=" + Hash(sourceInput) + "\nmeta=" + Hash(metaInput) +
                "\nshader=" + Hash(shaderInput) + "\nshaderMeta=" + Hash(shaderMetaInput) + "\n");
            value.importer = kShaderMetaImporterVersion;
            value.representation = ck::kShaderMetaDocumentRepresentation;
            value.schema = ck::kShaderMetaDocumentVersion;
            return value;
        }

        PreparedArtifact PrepareMaterial(const AssetSource& authored,
            const std::filesystem::path& assetRoot, InputCapture& capture)
        {
            const bool program = authored.asset.kind == ck::CookedAssetKind::MaterialProgram;
            const auto source = Canonical(assetRoot / std::filesystem::u8path(authored.source));
            if (!ck::IsContainedPath(assetRoot, source) ||
                source.extension() != (program ? ".shadergraph" : ".asset"))
            {
                Fail("Material source has an unsupported extension or escapes Assets: " + authored.source);
            }
            auto metaPath = source;
            metaPath += ".meta";
            const auto& sourceInput = capture.Capture(source, ck::kMaterialGraphSourceMaxBytes).bytes;
            const auto& metaInput = capture.Capture(metaPath, ck::kMaterialAssetSetMetaMaxBytes).bytes;
            std::filesystem::path verifiedPath;
            std::vector<std::byte> verifiedInput;
            ck::MaterialAssetSetCookResult cooked;
            if (program)
            {
                verifiedPath = Canonical(assetRoot / std::filesystem::u8path(authored.verifiedProgram));
                if (!ck::IsContainedPath(assetRoot, verifiedPath))
                {
                    Fail("Verified program escapes Assets: " + authored.verifiedProgram);
                }
                verifiedInput = capture.Capture(verifiedPath, ck::kMaterialProgramAssetSetMaxBytes).bytes;
                cooked = ck::BuildMaterialProgramAssetSetProduct({ authored.asset.key.assetId,
                    sourceInput, metaInput, verifiedInput, {} });
            }
            else
            {
                cooked = ck::BuildMaterialAssetSetProduct({ authored.asset.key.assetId, sourceInput, metaInput });
            }
            if (!cooked.Succeeded())
            {
                Fail("Material source cook failed: " + authored.source + ": " + cooked.failure);
            }
            auto& product = *cooked.product;
            if (product.asset != authored.asset || !MatchesRecipeDependencies(authored.dependencies, product.dependencies))
            {
                Fail("Declared typed material dependencies differ from current authoring source: " + authored.source);
            }
            if (Read(source, ck::kMaterialGraphSourceMaxBytes) != sourceInput ||
                Read(metaPath, ck::kMaterialAssetSetMetaMaxBytes) != metaInput ||
                (program && Read(verifiedPath, ck::kMaterialProgramAssetSetMaxBytes) != verifiedInput))
            {
                Fail("Material source/meta/verified program changed during source cook: " + authored.source);
            }
            PreparedArtifact value;
            value.bytes = std::move(product.artifactBytes);
            value.extension = product.extension;
            // Hash path tokens separately so source-authored newlines cannot
            // make two role/path inventories ambiguous.
            value.inputDigest = Hash("sourcePath=" + Hash(authored.source) + "\nverifiedPath=" +
                Hash(authored.verifiedProgram) + "\ninputs=" +
                Hash(std::as_bytes(std::span(cooked.sourceInputsSha256))) + "\n");
            value.importer = program ? kMaterialProgramImporterVersion : kMaterialImporterVersion;
            value.representation = product.representation;
            value.schema = product.schemaVersion;
            return value;
        }

        std::map<ck::AssetIdentity, PreparedArtifact> PrepareSources(const AssetSet& definition,
            const std::set<ck::AssetIdentity>& included, const std::filesystem::path& assetRoot,
            const std::filesystem::path& cache, std::string_view toolFingerprint,
            std::vector<PendingReceipt>& receipts, AssetSetBuildResult& metrics)
        {
            std::map<ck::AssetIdentity, PreparedArtifact> result;
            std::map<std::string, std::vector<ck::TypedAssetReference>> modelSelections;
            for (const auto& identity : included)
            {
                const auto& authored = definition.sources.at(identity);
                const bool standalone = authored.asset.kind == ck::CookedAssetKind::MaterialProgram ||
                    !assets::IsUuidV8(authored.asset.key.assetId.value);
                if (!standalone)
                {
                    modelSelections[authored.source].push_back(authored.asset);
                    continue;
                }
                InputCapture capture{ assetRoot, {} };
                const auto source = Canonical(assetRoot / std::filesystem::u8path(authored.source));
                auto metaPath = source;
                metaPath += ".meta";
                capture.Capture(source);
                capture.Capture(metaPath, kMaxDefinitionBytes);
                if (!authored.verifiedProgram.empty())
                    capture.Capture(assetRoot / std::filesystem::u8path(authored.verifiedProgram), ck::kMaterialProgramAssetSetMaxBytes);
                const std::vector<ck::TypedAssetReference> selections{ authored.asset };
                const auto recipeKey = ImportRecipeKey(definition, selections, capture, toolFingerprint);
                if (ReuseImport(definition, selections, capture, cache, recipeKey, result))
                {
                    ++metrics.reusedImports;
                    continue;
                }
                ++metrics.recookedImports;
                if (authored.asset.kind == ck::CookedAssetKind::ShaderMeta)
                {
                    result.emplace(identity, PrepareShaderMeta(authored, assetRoot, capture));
                    receipts.push_back(MakeReceipt(definition, selections, capture, cache, recipeKey, result));
                    continue;
                }
                if (authored.asset.kind == ck::CookedAssetKind::MaterialProgram ||
                    authored.asset.kind == ck::CookedAssetKind::Material)
                {
                    result.emplace(identity, PrepareMaterial(authored, assetRoot, capture));
                    receipts.push_back(MakeReceipt(definition, selections, capture, cache, recipeKey, result));
                    continue;
                }
                const auto& sourceInput = capture.Capture(source).bytes;
                const auto& metaInput = capture.Capture(metaPath, kMaxDefinitionBytes).bytes;
                experiment::AssetId capturedTextureId;
                std::vector<ck::ModelIdentityIssue> identityIssues;
                if (!ck::ReadAssetIdFromMeta(
                    { reinterpret_cast<const char*>(metaInput.data()), metaInput.size() }, capturedTextureId, identityIssues) ||
                    capturedTextureId != identity.assetId)
                {
                    Fail("Captured texture sidecar differs from declared identity: " + authored.source);
                }
                auto cooked = ck::BuildTextureCookProduct({ source, assetRoot });
                if (!cooked.Succeeded())
                {
                    std::string failure = "Texture source cook failed: " + authored.source;
                    for (const auto& issue : cooked.issues)
                    {
                        failure += "\n  " + issue.context + ": " + issue.message;
                    }
                    Fail(failure);
                }
                auto& product = *cooked.product;
                if (product.textureAssetId != identity.assetId || product.artifactBytes != sourceInput ||
                    Read(metaPath, kMaxDefinitionBytes) != metaInput || Read(source) != sourceInput)
                {
                    Fail("Source/meta changed during cook or definition assetId differs from sidecar: " + authored.source);
                }
                PreparedArtifact value;
                value.bytes = std::move(product.artifactBytes);
                value.extension = product.sourceExtension;
                value.inputDigest = Hash("source=" + Hash(sourceInput) + "\nmeta=" + Hash(metaInput) + "\n");
                value.importer = kTextureImporterVersion;
                value.representation = kTextureSourceImage;
                value.schema = ck::kTextureArtifactVersion;
                result.emplace(identity, std::move(value));
                receipts.push_back(MakeReceipt(definition, selections, capture, cache, recipeKey, result));
            }
            for (const auto& [relativeSource, selections] : modelSelections)
            {
                ck::ModelAssetSetCookRequest request;
                request.assetRoot = assetRoot;
                request.sourcePath = Canonical(assetRoot / std::filesystem::u8path(relativeSource));
                request.identityHeaderPath = assetRoot.parent_path() / "ProjectSetting" / "AssetIdentity.asset";
                request.selected = selections;
                InputCapture capture{ assetRoot, Canonical(request.identityHeaderPath) };
                capture.Capture(request.sourcePath);
                auto metaPath = request.sourcePath;
                metaPath += ".meta";
                capture.Capture(metaPath, kMaxDefinitionBytes);
                capture.Capture(request.identityHeaderPath, 1024u * 1024u);
                const auto recipeKey = ImportRecipeKey(definition, selections, capture, toolFingerprint);
                if (ReuseImport(definition, selections, capture, cache, recipeKey, result))
                {
                    ++metrics.reusedImports;
                    continue;
                }
                ++metrics.recookedImports;
                // The existing captured FBX/glTF resolver fails on any missing
                // attempted dependency. No successful import has unrecorded
                // negative probes or a fallback search outside this callback.
                request.captureSource = [&](const std::filesystem::path& path,
                    std::vector<std::byte>& bytes, std::string& failure)
                {
                    try
                    {
                        bytes = capture.Capture(path).bytes;
                        return true;
                    }
                    catch (const std::exception& error)
                    {
                        failure = error.what();
                        return false;
                    }
                };
                auto cooked = ck::BuildModelAssetSetProducts(request);
                if (!cooked.Succeeded())
                {
                    Fail("Model source cook failed: " + relativeSource + ": " + cooked.failure);
                }
                for (const auto& warning : cooked.warnings)
                {
                    std::cerr << "AssetSet source warning: " << relativeSource << ": " << warning << '\n';
                }
                for (auto& product : cooked.products)
                {
                    const auto& authored = definition.sources.at(product.asset.key);
                    if (authored.asset != product.asset || !MatchesRecipeDependencies(authored.dependencies, product.dependencies))
                    {
                        Fail("Authored typed dependencies differ from source: " + relativeSource + " [" +
                            Label(product.asset.key) + "]; skeleton requires [], clip requires exactly Hard skeleton, "
                            "static Mesh requires [], skinned Mesh requires exactly Hard skeleton; "
                            "Material requires its authored Hard MaterialProgram; embedded Texture requires []; "
                            "Model requires exactly Loadable skeleton, all authored clips, meshes and materials");
                    }
                    PreparedArtifact value;
                    value.bytes = std::move(product.artifactBytes);
                    value.extension = product.extension;
                    value.inputDigest = Hash(std::as_bytes(std::span(cooked.sourceInputsSha256)));
                    value.importer = kModelImporterVersion;
                    value.representation = product.representation;
                    value.schema = product.schemaVersion;
                    result.emplace(product.asset.key, std::move(value));
                }
                receipts.push_back(MakeReceipt(definition, selections, capture, cache, recipeKey, result));
            }
            // producer 내부 값만 비교하면 복사된 UUID 뒤의 다른 skeleton을 놓친다.
            // 실제 선택 artifact끼리 연결을 검증한다.
            // External children deliberately have no local bytes. Their typed
            // references remain in the manifest; captured-union resolution and
            // runtime binding validate those children without authoring fallback.
            for (const auto& identity : included)
            {
                const auto& authored = definition.sources.at(identity);
                std::string failure;
                if (authored.asset.kind == ck::CookedAssetKind::AnimationClip)
                {
                    ck::AnimationClipArtifact clip;
                    ck::SkeletonArtifact skeleton;
                    if (!ck::ReadAnimationClipArtifact(result.at(identity).bytes, clip, failure))
                    {
                        Fail("Prepared clip validation failed: " + failure);
                    }
                    const ck::AssetIdentity skeletonKey{ clip.skeletonAssetId, {} };
                    if (IsExternalDependency(authored, { skeletonKey, ck::CookedAssetKind::Skeleton }))
                    {
                        continue;
                    }
                    const auto found = result.find(skeletonKey);
                    if (found == result.end() || definition.sources.at(skeletonKey).asset.kind != ck::CookedAssetKind::Skeleton ||
                        !ck::ReadSkeletonArtifact(found->second.bytes, skeleton, failure) ||
                        !ck::ValidateAnimationClipBinding(clip, skeleton, failure))
                    {
                        Fail("Selected clip/skeleton layout binding failed: " + authored.source + ": " + failure);
                    }
                }
                else if (authored.asset.kind == ck::CookedAssetKind::Mesh)
                {
                    ck::ModelGeometryArtifact geometry;
                    if (!ck::ReadModelGeometryArtifact(result.at(identity).bytes, geometry, failure))
                    {
                        Fail("Prepared geometry validation failed: " + failure);
                    }
                    if (geometry.requiredBoneCount == 0u)
                    {
                        if (!authored.dependencies.empty())
                        {
                            Fail("Static geometry must not have a skeleton hard edge: " + authored.source);
                        }
                        continue;
                    }
                    if (authored.dependencies.size() != 1u ||
                        authored.dependencies[0].kind != ck::AssetDependencyKind::Hard ||
                        authored.dependencies[0].target.kind != ck::CookedAssetKind::Skeleton)
                    {
                        Fail("Skinned geometry requires exactly one typed Hard skeleton edge: " + authored.source);
                    }
                    const auto key = authored.dependencies[0].target.key;
                    if (authored.dependencies[0].scope == ck::AssetDependencyScope::External)
                    {
                        continue;
                    }
                    const auto found = result.find(key);
                    ck::SkeletonArtifact skeleton;
                    if (found == result.end() || definition.sources.at(key).asset.kind != ck::CookedAssetKind::Skeleton ||
                        !ck::ReadSkeletonArtifact(found->second.bytes, skeleton, failure) ||
                        !ck::ValidateModelGeometryBinding(geometry, skeleton, failure))
                    {
                        Fail("Selected geometry/skeleton full binding failed: " + authored.source + ": " + failure);
                    }
                }
                else if (authored.asset.kind == ck::CookedAssetKind::Model)
                {
                    ck::ModelDescriptorArtifact descriptor;
                    if (!ck::ReadModelDescriptorArtifact(result.at(identity).bytes, descriptor, failure) ||
                        descriptor.modelAssetId != identity.assetId ||
                        !ck::ValidateModelDescriptorDependencies(descriptor, authored.dependencies, failure))
                    {
                        Fail("Selected model descriptor closure failed: " + authored.source + ": " + failure);
                    }
                    for (const auto& summary : descriptor.meshes)
                    {
                        const ck::AssetIdentity key{ summary.meshAssetId, {} };
                        if (IsExternalDependency(authored, { key, ck::CookedAssetKind::Mesh }))
                        {
                            continue;
                        }
                        const auto found = result.find(key);
                        ck::ModelGeometryArtifact geometry;
                        if (found == result.end() || definition.sources.at(key).asset.kind != ck::CookedAssetKind::Mesh ||
                            !ck::ReadModelGeometryArtifact(found->second.bytes, geometry, failure) ||
                            !ck::ValidateModelMeshSummary(summary, geometry, failure))
                        {
                            Fail("Selected model/mesh summary mismatch: " + authored.source + ": " + failure);
                        }
                        const auto& edges = definition.sources.at(key).dependencies;
                        if (summary.skinned && (edges.size() != 1u ||
                            edges[0].target.key.assetId != descriptor.skeletonAssetId))
                        {
                            Fail("Selected model and mesh disagree on logical skeleton identity: " + authored.source);
                        }
                    }
                }
                else if (authored.asset.kind == ck::CookedAssetKind::Material)
                {
                    material_graph::InstanceDocument document;
                    if (!ck::ReadMaterialAssetSetDocument(result.at(identity).bytes, authored.asset,
                        authored.dependencies, document, failure))
                    {
                        Fail("Selected Material document failed source-free validation: " + failure);
                    }
                    const ck::AssetIdentity programIdentity{ document.description.graphId, {} };
                    if (IsExternalDependency(authored, { programIdentity, ck::CookedAssetKind::MaterialProgram }))
                    {
                        continue;
                    }
                    const auto programSource = definition.sources.find(programIdentity);
                    const auto programArtifact = result.find(programIdentity);
                    material_graph::CookedProgram program;
                    if (programSource == definition.sources.end() || programArtifact == result.end() ||
                        programSource->second.asset.kind != ck::CookedAssetKind::MaterialProgram ||
                        !ck::ReadMaterialProgramAssetSetArtifact(programArtifact->second.bytes,
                            programSource->second.asset, programSource->second.dependencies, {}, program, failure) ||
                        !ck::ValidateMaterialAssetSetBinding(document, program.product, failure))
                    {
                        Fail("Selected Material/MaterialProgram binding failed: " + authored.source + ": " + failure);
                    }
                }
            }
            return result;
        }

    }

    AssetSetBuildResult BuildAssetSet(const AssetSetBuildRequest& request)
    {
        AssetSetBuildResult result;
        try
        {
            if (request.toolFingerprint.size() != 64u || !std::ranges::all_of(request.toolFingerprint, [](char ch)
                { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }))
            {
                Fail("BuildAssetSet requires the SHA256 fingerprint of its verified toolchain");
            }
            const auto assetRoot = Canonical(request.assetRoot);
            const auto definitionPath = Canonical(request.definitionPath);
            const auto output = Canonical(request.outputRoot);
            const auto cache = Canonical(request.artifactCache);
            if (!std::filesystem::is_directory(assetRoot) || std::filesystem::exists(output))
            {
                Fail("BuildAssetSet requires an existing source root and a new output directory");
            }
            Disjoint(assetRoot, output);
            Disjoint(assetRoot, cache);
            Disjoint(output, cache);
            if (ck::IsContainedPath(output, definitionPath) || ck::IsContainedPath(cache, definitionPath))
            {
                Fail("The authoring definition cannot reside in output or artifact cache");
            }
            InputCapture definitionCapture;
            definitionCapture.Capture(definitionPath, kMaxDefinitionBytes, true);
            const auto definition = ParseDefinition(definitionCapture.files.at(definitionPath).bytes);
            const auto included = IncludeClosure(definition);
            RejectHardCycles(definition, included);
            std::filesystem::create_directories(cache);
            // The former prototype directory lock is deliberately not consulted
            // or deleted. A leftover directory cannot own this OS-scoped guard.
            const AssetSetCacheLock cacheLock{ cache / ".asset-set-build.guard" };
            std::filesystem::create_directories(output.parent_path());
            AssetDepot::ArtifactStoreGuard publicationGuard;
            std::string guardFailure;
            if (!AssetDepot::ArtifactStoreGuard::BeginPublication(output, publicationGuard, guardFailure))
            {
                Fail("Cannot reserve immutable AssetSet backing: " + guardFailure);
            }
            const auto nonce = std::to_string(GetCurrentProcessId()) + "." +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            // Killed processes may leave these unique work directories. They are
            // never enumerated as cached results, resumed, or deleted by a later
            // invocation. Only exact immutable blob/key paths are reuse inputs.
            const auto cacheWork = cache / (".asset-set-work-" + nonce + ".incomplete");
            if (!std::filesystem::create_directory(cacheWork))
            {
                Fail("Cannot reserve new AssetSet cache work directory");
            }
            DirectoryScope cacheWorkScope{ cacheWork };
            const auto candidate = output.parent_path() / (".asset-set-" + nonce + ".candidate");
            if (!std::filesystem::create_directory(candidate))
            {
                Fail("Cannot reserve new AssetSet candidate directory");
            }
            DirectoryScope candidateScope{ candidate };
            std::vector<PendingReceipt> receipts;
            const auto prepared = PrepareSources(definition, included, assetRoot, cache,
                request.toolFingerprint, receipts, result);
            definitionCapture.Verify();
            ck::AssetSetManifest manifest;
            manifest.assetSetId = definition.assetSetId;
            manifest.revision = definition.revision;
            manifest.targetPlatform = definition.targetPlatform;
            manifest.targetAbi = definition.targetAbi;
            manifest.roots = definition.roots;
            std::map<std::string, std::uint32_t> blobIndices;
            std::string buildRecords = std::string(kBuildVersion) + "\n";
            for (const auto& identity : included)
            {
                const auto& authored = definition.sources.at(identity);
                const auto& product = prepared.at(identity);
                const auto digest = Hash(product.bytes);
                const auto compatibility = Compatibility(definition, authored.asset.kind,
                    product.representation, product.schema);
                // This is the pre-import recipe + complete captured inventory
                // key, including target/settings and external source inputs.
                const auto& importKey = product.importKey;
                const auto artifactInput = authored.asset.kind == ck::CookedAssetKind::Texture &&
                    experiment::IsAssetIdV4(authored.asset.key.assetId)
                    ? product.inputDigest : digest;
                const auto buildKey = Hash(std::string(kBuildVersion) + "\nimporter=" + product.importer +
                    "\ntool=" + request.toolFingerprint + "\n" + compatibility +
                    "textureEncoding=Source\nextension=" + product.extension +
                    "\nselectedInput=" + artifactInput + "\nselection=" + Label(identity) + "\n");
                const auto relative = "Derived/AssetBlobs/" + Hash(compatibility) + "/" + digest + product.extension;
                const auto record = "buildKey=" + buildKey + "\ncontentSha256=" + digest +
                    "\nbyteSize=" + std::to_string(product.bytes.size()) + "\nartifactPath=" + relative + "\n";
                const auto recordPath = cache / "BuildKeys" / (buildKey + ".txt");
                if (std::filesystem::exists(recordPath))
                {
                    const auto existing = Read(recordPath, 4096u);
                    if (std::string(reinterpret_cast<const char*>(existing.data()), existing.size()) != record)
                    {
                        Fail("Immutable build-key record differs from source cook: " + recordPath.string());
                    }
                }
                const auto cachedBlob = cache / std::filesystem::u8path(relative);
                const bool cached = std::filesystem::exists(cachedBlob);
                if (cached)
                {
                    VerifyBytes(cachedBlob, product.bytes.size(), digest, authored.asset, authored.dependencies, product.extension);
                }
                else
                {
                    ValidatePayload(product.bytes, authored.asset, authored.dependencies, product.extension);
                    // Cache-local temporary storage keeps rename on the same
                    // volume, even when the requested output uses another drive.
                    const auto cacheTemporary = cacheWork / "payload.tmp";
                    WriteNew(cacheTemporary, product.bytes);
                    VerifyBytes(cacheTemporary, product.bytes.size(), digest, authored.asset, authored.dependencies, product.extension);
                    std::filesystem::create_directories(cachedBlob.parent_path());
                    NoReparse(cachedBlob);
                    std::filesystem::rename(cacheTemporary, cachedBlob);
                }
                if (!std::filesystem::exists(recordPath))
                {
                    std::filesystem::create_directories(recordPath.parent_path());
                    const auto temporaryRecord = cacheWork / "build-key.tmp";
                    WriteNew(temporaryRecord, record);
                    std::filesystem::rename(temporaryRecord, recordPath);
                }
                auto [blob, inserted] = blobIndices.emplace(relative,
                    static_cast<std::uint32_t>(manifest.blobs.size()));
                if (inserted)
                {
                    ck::AssetBlobRecord blobRecord;
                    std::string hashFailure;
                    if (!ck::ComputeSha256(product.bytes, blobRecord.contentSha256, hashFailure))
                    {
                        Fail("Cannot hash AssetSet payload: " + hashFailure);
                    }
                    blobRecord.byteSize = product.bytes.size();
                    blobRecord.kind = authored.asset.kind;
                    blobRecord.representation = product.representation;
                    blobRecord.schemaVersion = product.schema;
                    blobRecord.targetPlatform = definition.targetPlatform;
                    blobRecord.targetAbi = definition.targetAbi;
                    blobRecord.artifactPath = relative;
                    manifest.blobs.push_back(blobRecord);
                    const auto outputBlob = candidate / std::filesystem::u8path(relative);
                    std::filesystem::create_directories(outputBlob.parent_path());
                    // Output owns independent immutable files, so cache GC cannot break a release.
                    std::filesystem::copy_file(cachedBlob, outputBlob);
                    VerifyBytes(outputBlob, product.bytes.size(), digest, authored.asset, authored.dependencies, product.extension);
                    if (cached)
                    {
                        ++result.reusedBlobs;
                    }
                }
                manifest.entries.push_back({ authored.asset, blob->second, authored.dependencies });
                buildRecords += Label(identity) + " " + buildKey + " sourceImportKey=" + importKey + "\n";
            }
            const auto written = ck::WriteAssetSetManifest(manifest);
            if (!written.Succeeded())
            {
                std::string failure = "AssetSet manifest validation failed";
                for (const auto& issue : written.issues)
                {
                    failure += "\n  " + issue.context + ": " + issue.message;
                }
                Fail(failure);
            }
            const auto manifestPath = candidate / "Derived" / "asset-set-manifest.cemf";
            WriteNew(manifestPath, written.bytes);
            ck::AssetSetManifest readback;
            std::vector<ck::AssetManifestIssue> issues;
            const auto manifestBytes = Read(manifestPath, ck::kAssetSetManifestMaxBytes);
            if (manifestBytes != written.bytes || !ck::ReadAssetSetManifest(manifestBytes, readback, issues))
            {
                Fail("AssetSet manifest readback failed");
            }
            for (const auto& receipt : receipts)
            {
                NoReparse(receipt.path);
                if (std::filesystem::exists(receipt.path))
                {
                    if (Read(receipt.path, kMaxReceiptBytes) != receipt.bytes)
                        Fail("Immutable import receipt differs: " + receipt.path.string());
                    continue;
                }
                // Never enumerate work files as hits. Process termination can
                // leave only an ignored candidate or the complete final record.
                const auto temporary = cacheWork / "import-receipt.tmp";
                WriteNew(temporary, receipt.bytes);
                if (Read(temporary, kMaxReceiptBytes) != receipt.bytes)
                    Fail("Import receipt candidate readback failed");
                std::filesystem::create_directories(receipt.path.parent_path());
                NoReparse(receipt.path);
                std::filesystem::rename(temporary, receipt.path);
            }
            WriteNew(candidate / "build-keys.txt", buildRecords);
            const std::string report = "format=CEMF3\nassets=" + std::to_string(manifest.entries.size()) +
                "\nblobs=" + std::to_string(manifest.blobs.size()) +
                "\nmanifestSha256=" + Hash(manifestBytes) +
                "\nbuildKeysSha256=" + Hash(buildRecords) + "\n";
            WriteNew(candidate / "build-report.txt", report);
            // No mutable release pointer and no replacement: caller receives this exact revision.
            std::filesystem::rename(candidate, output);
            candidateScope.path.clear();
            if (!publicationGuard.CommitPublication(guardFailure))
            {
                Fail("Published AssetSet has no valid storage lease enrollment: " + guardFailure);
            }
            result.assets = manifest.entries.size();
            result.blobs = manifest.blobs.size();
            result.succeeded = true;
            std::cout << "AssetSet imports reusedImports=" << result.reusedImports
                << " recookedImports=" << result.recookedImports << '\n';
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
        }
        return result;
    }
}
