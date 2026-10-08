#include "AssetSetBuild.h"

#include "AuthoringParsedDocument.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookSupport.h"
#include "Experiment/Cooked/TextureCookProducer.h"
#include "Experiment/Cooked/ModelAssetSetProducer.h"
#include "Texture.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <iostream>
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
        constexpr std::string_view kModelImporterVersion = "model-source-subassets-v2";
        constexpr std::string_view kBuildVersion = "asset-set-build-v3";
        constexpr std::uint32_t kTextureSourceImage = 1u;

        struct AssetSource final
        {
            ck::TypedAssetReference asset{};
            std::string source{};
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
            if (name == "Texture")
            {
                return { { Id(node, "assetId"), {} }, ck::CookedAssetKind::Texture };
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
                Fail("Unsupported AssetSet source kind: " + name + "; supported: Texture, Model, Mesh, Skeleton, AnimationClip");
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

        AssetSet ParseDefinition(const std::filesystem::path& path)
        {
            const auto bytes = Read(path, kMaxDefinitionBytes);
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
                RequireMap(node, { "assetId", "kind", "source", "dependencies" }, "asset source");
                AssetSource source;
                source.asset = Reference(node);
                source.source = Text(node, "source");
                if (!RelativePath(source.source))
                {
                    Fail("Asset source must be normalized relative to Assets: " + source.source);
                }
                const auto dependencies = node["dependencies"];
                if (!dependencies.IsSequence())
                {
                    Fail("Every source must explicitly declare dependencies, including [] for leaves: " +
                        source.source);
                }
                std::set<ck::AssetDependency> distinctDependencies;
                for (const auto edge : dependencies)
                {
                    if (++edgeCount > kMaxEdges)
                    {
                        Fail("AssetSet source exceeds 262144 dependency edges");
                    }
                    RequireMap(edge, { "assetId", "kind", "dependency" }, "dependency");
                    ck::AssetDependency dependency;
                    dependency.target = Reference(edge);
                    const auto kind = Text(edge, "dependency");
                    if (kind != "Hard" && kind != "Loadable")
                    {
                        Fail("Dependency must explicitly be Hard or Loadable: " + source.source);
                    }
                    dependency.kind = kind == "Hard"
                        ? ck::AssetDependencyKind::Hard : ck::AssetDependencyKind::Loadable;
                    dependency.scope = ck::AssetDependencyScope::Internal;
                    if (!distinctDependencies.insert(dependency).second)
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
                        // Build includes both; runtime eager loading follows only Hard.
                        pending.push_back(edge.target);
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
                        if (edge.kind != ck::AssetDependencyKind::Hard)
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
                            return edge.kind == ck::AssetDependencyKind::Hard && edge.target.key == key;
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

        void ValidatePayload(std::span<const std::byte> bytes, ck::CookedAssetKind kind,
            const std::string& extension)
        {
            std::string failure;
            bool valid{};
            switch (kind)
            {
            case ck::CookedAssetKind::Texture:
                ValidateTexture(bytes, extension);
                return;
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
            const std::string& digest, ck::CookedAssetKind kind, const std::string& extension)
        {
            const auto bytes = Read(path);
            if (bytes.size() != size || Hash(bytes) != digest)
            {
                Fail("Immutable AssetSet blob hash/size mismatch: " + path.string());
            }
            ValidatePayload(bytes, kind, extension);
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
            std::string importer{};
            std::uint32_t representation{};
            std::uint32_t schema{};
        };

        std::map<ck::AssetIdentity, PreparedArtifact> PrepareSources(const AssetSet& definition,
            const std::set<ck::AssetIdentity>& included, const std::filesystem::path& assetRoot)
        {
            std::map<ck::AssetIdentity, PreparedArtifact> result;
            std::map<std::string, std::vector<ck::TypedAssetReference>> modelSelections;
            for (const auto& identity : included)
            {
                const auto& authored = definition.sources.at(identity);
                if (authored.asset.kind != ck::CookedAssetKind::Texture)
                {
                    modelSelections[authored.source].push_back(authored.asset);
                    continue;
                }
                const auto source = Canonical(assetRoot / std::filesystem::u8path(authored.source));
                if (!ck::IsContainedPath(assetRoot, source))
                {
                    Fail("Source escapes asset root: " + authored.source);
                }
                const auto sourceInput = Read(source);
                auto metaPath = source;
                metaPath += ".meta";
                const auto metaInput = Read(metaPath, kMaxDefinitionBytes);
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
            }
            for (const auto& [relativeSource, selections] : modelSelections)
            {
                ck::ModelAssetSetCookRequest request;
                request.assetRoot = assetRoot;
                request.sourcePath = Canonical(assetRoot / std::filesystem::u8path(relativeSource));
                request.identityHeaderPath = assetRoot.parent_path() / "ProjectSetting" / "AssetIdentity.asset";
                request.selected = selections;
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
                    if (authored.asset != product.asset || authored.dependencies != product.dependencies)
                    {
                        Fail("Authored typed dependencies differ from source: " + relativeSource + " [" +
                            Label(product.asset.key) + "]; skeleton requires [], clip requires exactly Hard skeleton, "
                            "static Mesh requires [], skinned Mesh requires exactly Hard skeleton; "
                            "Model requires exactly Loadable skeleton, all authored clips and meshes");
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
            }
            // producer 내부 값만 비교하면 복사된 UUID 뒤의 다른 skeleton을 놓친다.
            // 실제 선택 artifact끼리 연결을 검증한다.
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
            const auto definition = ParseDefinition(definitionPath);
            const auto included = IncludeClosure(definition);
            RejectHardCycles(definition, included);
            const auto prepared = PrepareSources(definition, included, assetRoot);
            std::filesystem::create_directories(cache);
            // The former prototype directory lock is deliberately not consulted
            // or deleted. A leftover directory cannot own this OS-scoped guard.
            const AssetSetCacheLock cacheLock{ cache / ".asset-set-build.guard" };
            std::filesystem::create_directories(output.parent_path());
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
                // Full captured inputs identify the import transaction. Per-artifact
                // keys use only normalized selected output, so another clip or a
                // sidecar generation edit does not invalidate unchanged clip work.
                const auto importKey = Hash("source-import-v1\nimporter=" + product.importer +
                    "\ntool=" + request.toolFingerprint + "\ninputs=" + product.inputDigest + "\n");
                const auto artifactInput = authored.asset.kind == ck::CookedAssetKind::Texture
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
                    VerifyBytes(cachedBlob, product.bytes.size(), digest, authored.asset.kind, product.extension);
                }
                else
                {
                    ValidatePayload(product.bytes, authored.asset.kind, product.extension);
                    // Cache-local temporary storage keeps rename on the same
                    // volume, even when the requested output uses another drive.
                    const auto cacheTemporary = cacheWork / "payload.tmp";
                    WriteNew(cacheTemporary, product.bytes);
                    VerifyBytes(cacheTemporary, product.bytes.size(), digest, authored.asset.kind, product.extension);
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
                    VerifyBytes(outputBlob, product.bytes.size(), digest, authored.asset.kind, product.extension);
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
            WriteNew(candidate / "build-keys.txt", buildRecords);
            const std::string report = "format=CEMF3\nassets=" + std::to_string(manifest.entries.size()) +
                "\nblobs=" + std::to_string(manifest.blobs.size()) +
                "\nmanifestSha256=" + Hash(manifestBytes) +
                "\nbuildKeysSha256=" + Hash(buildRecords) + "\n";
            WriteNew(candidate / "build-report.txt", report);
            // No mutable release pointer and no replacement: caller receives this exact revision.
            std::filesystem::rename(candidate, output);
            candidateScope.path.clear();
            result.assets = manifest.entries.size();
            result.blobs = manifest.blobs.size();
            result.succeeded = true;
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
        }
        return result;
    }
}
