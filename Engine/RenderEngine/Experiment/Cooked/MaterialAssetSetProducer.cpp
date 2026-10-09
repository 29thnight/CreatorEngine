#include "MaterialAssetSetProducer.h"

#include "CookedMaterialProgram.h"
#include "../../Assets/AssetIdentityProfile.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringCookedDocument.h"
#include "../MaterialAuthoringCodec.h"

#include <algorithm>

#include <exception>
#include <initializer_list>
#include <set>
#include <map>
#include <cctype>
#include <utility>

namespace experiment::cooked
{
    namespace material_asset_set_producer_detail
    {
        bool Fail(std::string& failure, std::string message)
        {
            failure = std::move(message);
            return false;
        }

        std::string Text(std::span<const std::byte> bytes)
        {
            return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        }

        bool ValidateMeta(std::span<const std::byte> bytes, const AssetId& expected, std::string& failure)
        {
            if ((!IsAssetIdV4(expected) && !assets::IsUuidV8(expected.value)) ||
                bytes.empty() || bytes.size() > kMaterialAssetSetMetaMaxBytes)
            {
                return Fail(failure, "Material recipe requires a UUIDv4/v8 identity and a bounded captured sidecar.");
            }
            const auto document = Authoring::ParsedDocument::ParseText(Text(bytes), failure);
            if (!document)
            {
                return false;
            }
            const auto root = document.Root();
            if (!root.IsMap())
            {
                return Fail(failure, "Material sidecar root must be a mapping.");
            }
            std::set<std::string> keys;
            for (const auto entry : root.Map())
            {
                if (!entry.key.IsScalar() || !keys.insert(entry.key.AsString()).second)
                {
                    return Fail(failure, "Material sidecar contains duplicate or nonscalar keys.");
                }
            }
            const auto guid = root["guid"];
            AssetId parsed;
            if (!guid.IsScalar() ||
                (!TryParseCanonicalAssetId(guid.Scalar(), parsed) &&
                    !assets::TryParseCanonicalUuidV8(guid.Scalar(), parsed.value)) || parsed != expected)
            {
                return Fail(failure, "Material sidecar GUID must be canonical and match the requested UUIDv4/v8.");
            }
            return true;
        }

        void AppendSize(std::vector<std::byte>& bytes, std::uint64_t value)
        {
            for (unsigned shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }
        }

        void AppendText(std::vector<std::byte>& bytes, std::string_view value)
        {
            AppendSize(bytes, value.size());
            for (const char character : value)
            {
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
            }
        }

        bool AppendInput(std::vector<std::byte>& inventory, std::string_view role,
            std::span<const std::byte> bytes, std::string& failure)
        {
            Sha256Digest digest;
            if (!ComputeSha256(bytes, digest, failure))
            {
                return false;
            }
            AppendText(inventory, role);
            AppendSize(inventory, bytes.size());
            for (const auto value : digest)
            {
                inventory.push_back(static_cast<std::byte>(value));
            }
            return true;
        }

        bool InputDigest(std::string_view domain, std::span<const std::byte> source,
            std::span<const std::byte> meta, std::span<const std::byte> verified,
            Sha256Digest& out, std::string& failure)
        {
            std::vector<std::byte> inventory;
            AppendText(inventory, domain);
            AppendSize(inventory, verified.empty() ? 2u : 3u);
            if (!AppendInput(inventory, "source", source, failure) ||
                !AppendInput(inventory, "meta", meta, failure))
            {
                return false;
            }
            if (!verified.empty() && !AppendInput(inventory, "verified-program", verified, failure))
            {
                return false;
            }
            return ComputeSha256(inventory, out, failure);
        }
    }

    namespace material_asset_set_producer_detail
    {
        bool ExactMap(const Authoring::ReadNode& node, std::initializer_list<std::string_view> expected,
            std::string& failure)
        {
            if (!node.IsMap())
            {
                return Fail(failure, "Code recipe must be a mapping.");
            }
            std::set<std::string> keys;
            for (const auto entry : node.Map())
            {
                if (!entry.key.IsScalar() || !keys.insert(entry.key.AsString()).second ||
                    std::ranges::find(expected, entry.key.Scalar()) == expected.end())
                {
                    return Fail(failure, "Code recipe contains duplicate, nonscalar or unknown fields.");
                }
            }
            if (keys.size() != expected.size())
            {
                return Fail(failure, "Code recipe omits a required field.");
            }
            return true;
        }

        // Conservative source closure for this recipe: literal #include only.
        // Macro includes, Slang module imports and file-probing directives are
        // rejected rather than pretending the claimed inventory proves them.
        bool IncludeClosure(std::span<const CodeProgramCapturedInput> inputs, const CodeProgram& program,
            std::string& failure)
        {
            std::map<std::string, std::span<const std::byte>> files;
            for (const auto& input : inputs)
            {
                if (input.path != program.metadataPath && input.path != program.metadataPath + ".meta" &&
                    input.path != program.rootSourcePath + ".meta")
                {
                    files.emplace(input.path, input.bytes);
                }
            }
            std::set<std::string> visited;
            std::vector<std::string> pending{program.rootSourcePath};
            while (!pending.empty())
            {
                auto path = std::move(pending.back()); pending.pop_back();
                if (!visited.insert(path).second)
                {
                    continue;
                }
                const auto found = files.find(path);
                if (found == files.end())
                {
                    return Fail(failure, "Code compiler inventory omits a literal include: " + path);
                }
                auto source = Text(found->second);
                if (source.find('\0') != source.npos)
                {
                    return Fail(failure, "Code source contains embedded NUL bytes.");
                }
                // Translation phase 2 before comment removal.
                for (std::size_t i = 0; i + 1u < source.size();)
                {
                    if (source[i] == '\\' && source[i + 1u] == '\n')
                    {
                        source.erase(i, 2u);
                    }
                    else if (i + 2u < source.size() && source[i] == '\\' && source[i + 1u] == '\r' && source[i + 2u] == '\n')
                    {
                        source.erase(i, 3u);
                    }
                    else
                    {
                        ++i;
                    }
                }
                bool block{}, line{}, quoted{}; char quote{};
                for (std::size_t i = 0; i < source.size(); ++i)
                {
                    const auto ch = source[i];
                    const auto next = i + 1u < source.size() ? source[i + 1u] : '\0';
                    if (line)
                    {
                        if (ch == '\n')
                        {
                            line = false;
                        }
                        else
                        {
                            source[i] = ' ';
                        }
                        continue;
                    }
                    if (block)
                    {
                        if (ch == '*' && next == '/')
                        {
                            source[i] = source[i + 1u] = ' ';
                            ++i;
                            block = false;
                        }
                        else if (ch != '\n')
                        {
                            source[i] = ' ';
                        }
                        continue;
                    }
                    if (quoted)
                    {
                        if (ch == '\\' && i + 1u < source.size())
                        {
                            ++i;
                            continue;
                        }
                        if (ch == quote)
                        {
                            quoted = false;
                        }
                        continue;
                    }
                    if (ch == '\"' || ch == '\'')
                    {
                        quoted = true;
                        quote = ch;
                        continue;
                    }
                    if (ch == '/' && (next == '/' || next == '*'))
                    {
                        source[i] = source[i + 1u] = ' ';
                        ++i;
                        line = next == '/';
                        block = next == '*';
                    }
                }
                if (block)
                {
                    return Fail(failure, "Code source has an unterminated block comment.");
                }
                const auto trim = [](std::string_view text)
                {
                    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                    {
                        text.remove_prefix(1);
                    }
                    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                    {
                        text.remove_suffix(1);
                    }
                    return text;
                };
                for (std::size_t position = 0; position < source.size();)
                {
                    if (source[position] == '\"' || source[position] == '\'')
                    {
                        const auto delimiter = source[position++];
                        while (position < source.size() && source[position] != delimiter)
                        {
                            if (source[position] == '\\' && position + 1u < source.size())
                            {
                                ++position;
                            }
                            ++position;
                        }
                        if (position < source.size())
                        {
                            ++position;
                        }
                        continue;
                    }
                    if (!std::isalpha(static_cast<unsigned char>(source[position])) && source[position] != '_')
                    {
                        ++position;
                        continue;
                    }
                    const auto start = position++;
                    while (position < source.size() && (std::isalnum(static_cast<unsigned char>(source[position])) || source[position] == '_'))
                    {
                        ++position;
                    }
                    const auto token = std::string_view(source).substr(start, position - start);
                    if (token == "import" || token == "__import" || token == "__include" || token == "include_alias" ||
                        token == "__has_include" || token == "__has_include_next")
                    {
                        return Fail(failure, "Code recipe cannot attest dynamic module/file lookup; use a literal-include verified source.");
                    }
                }
                std::size_t begin{};
                while (begin < source.size())
                {
                    const auto end = source.find('\n', begin);
                    auto text = trim(std::string_view(source).substr(begin, end == source.npos ? source.size() - begin : end - begin));
                    begin = end == source.npos ? source.size() : end + 1u;
                    if (text.empty() || text.front() != '#')
                    {
                        continue;
                    }
                    text.remove_prefix(1u); text = trim(text);
                    std::size_t count{};
                    while (count < text.size() && (std::isalnum(static_cast<unsigned char>(text[count])) || text[count] == '_'))
                    {
                        ++count;
                    }
                    const auto directive = text.substr(0u, count);
                    if (directive == "include_next" || directive == "embed")
                    {
                        return Fail(failure, "Code recipe does not support this external input directive.");
                    }
                    if (directive != "include")
                    {
                        continue;
                    }
                    text = trim(text.substr(count));
                    if (text.size() < 3u || (text.front() != '\"' && text.front() != '<'))
                    {
                        return Fail(failure, "Code recipe requires literal include paths; macro includes cannot prove source closure.");
                    }
                    const auto quoteEnd = text.find(text.front() == '<' ? '>' : '\"', 1u);
                    if (quoteEnd == text.npos || !trim(text.substr(quoteEnd + 1u)).empty())
                    {
                        return Fail(failure, "Code recipe include directive is malformed.");
                    }
                    const auto token = std::string(text.substr(1u, quoteEnd - 1u));
                    if (token.empty() || token.find('\\') != token.npos || token.find(':') != token.npos || token.front() == '/')
                    {
                        return Fail(failure, "Code recipe include path is not a portable relative token.");
                    }
                    const auto resolved = (text.front() == '<' ? std::filesystem::u8path(token) :
                        std::filesystem::u8path(path).parent_path() / std::filesystem::u8path(token)).lexically_normal().generic_string();
                    if (!files.contains(resolved))
                    {
                        return Fail(failure, "Verified code input inventory omits include: " + resolved);
                    }
                    pending.push_back(resolved);
                }
            }
            if (visited.size() != files.size())
            {
                return Fail(failure, "Code input inventory contains unreachable or unsupported compiler inputs.");
            }
            return true;
        }

        bool CodeInputs(std::span<const CodeProgramCapturedInput> inputs, const CodeProgram& program,
            std::string& failure)
        {
            if (inputs.size() != program.inputs.size())
            {
                return Fail(failure, "Code recipe capture omits or adds inputs relative to its verified bundle.");
            }
            const CodeProgramCapturedInput* metadata = nullptr;
            const CodeProgramCapturedInput* sidecar = nullptr;
            const CodeProgramCapturedInput* sourceSidecar = nullptr;
            for (std::size_t index = 0; index < program.inputs.size(); ++index)
            {
                const auto& actual = inputs[index];
                const auto& expected = program.inputs[index];
                Sha256Digest digest;
                if (actual.path != expected.path || actual.bytes.size() != expected.byteSize ||
                    !ComputeSha256(actual.bytes, digest, failure) || digest != expected.sha256)
                {
                    return Fail(failure, "Code recipe captured input differs from verified compiler inventory: " + expected.path);
                }
                if (actual.path == program.metadataPath)
                {
                    metadata = &actual;
                }
                if (actual.path == program.metadataPath + ".meta")
                {
                    sidecar = &actual;
                }
                if (actual.path == program.rootSourcePath + ".meta")
                {
                    sourceSidecar = &actual;
                }
            }
            if (!metadata || !sidecar || !sourceSidecar || !ValidateMeta(sidecar->bytes, program.shaderMetaAssetId, failure))
            {
                return false;
            }
            const auto sourceIdentity = Authoring::ParsedDocument::ParseText(Text(sourceSidecar->bytes), failure);
            AssetId sourceId;
            if (!sourceIdentity || !sourceIdentity.Root()["guid"].IsScalar() ||
                !TryParseCanonicalAssetId(sourceIdentity.Root()["guid"].Scalar(), sourceId) ||
                !ValidateMeta(sourceSidecar->bytes, sourceId, failure))
            {
                return Fail(failure, "Code root shader identity sidecar must contain a canonical UUIDv4.");
            }
            const auto parsed = Authoring::ParsedDocument::ParseText(Text(metadata->bytes), failure);
            std::vector<std::byte> canonical;
            if (!parsed || !Authoring::EncodeCookedDocument(parsed.Root(), canonical, failure) || canonical != program.metadataBytes)
            {
                return Fail(failure, "Code recipe captured metadata differs from its canonical verified descriptor.");
            }
            return IncludeClosure(inputs, program, failure);
        }

        MaterialAssetSetCookResult BuildCodeProgram(const MaterialProgramAssetSetCookRequest& request,
            const Authoring::ReadNode& root)
        {
            MaterialAssetSetCookResult result;
            if (!ExactMap(root, { "code_program" }, result.failure))
            {
                return result;
            }
            const auto node = root["code_program"];
            if (!ExactMap(node, { "schema", "shaderMetaAssetId", "metadata", "source", "compilerSha256" }, result.failure) ||
                !node["schema"].IsScalar() || node["schema"].As<std::uint32_t>() != kCodeProgramVersion)
            {
                if (result.failure.empty())
                {
                    result.failure = "Code recipe schema is incompatible.";
                }
                return result;
            }
            for (const auto field : { "shaderMetaAssetId", "metadata", "source", "compilerSha256" })
            {
                if (!node[field].IsScalar())
                {
                    result.failure = "Code recipe field must be scalar.";
                    return result;
                }
            }
            CodeProgram program;
            std::vector<AssetDependency> edges;
            if (!ReadVerifiedCodeProgram(request.verifiedProgramBytes, program, edges, result.failure))
            {
                return result;
            }
            AssetId metaId;
            if (!TryParseCanonicalAssetId(node["shaderMetaAssetId"].Scalar(), metaId) ||
                program.programAssetId != request.programAssetId || program.shaderMetaAssetId != metaId ||
                program.metadataPath != node["metadata"].Scalar() || program.rootSourcePath != node["source"].Scalar() ||
                program.compilerStamp != node["compilerSha256"].Scalar())
            {
                result.failure = "Code recipe identity, metadata/source paths or verified compiler SHA-256 differs from its explicit bundle.";
                return result;
            }
            if (!CodeInputs(request.codeInputs, program, result.failure))
            {
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.programAssetId, {} }, CookedAssetKind::MaterialProgram };
            if (!EncodeCodeProgramArtifact(program, product.artifactBytes, product.dependencies, result.failure))
            {
                return result;
            }
            product.representation = kCodeProgramRepresentation;
            product.schemaVersion = kCodeProgramVersion;
            product.extension = ".cecp";
            std::vector<std::byte> inventory;
            AppendText(inventory, "code-program-asset-set-source-v1");
            AppendSize(inventory, 3u + request.codeInputs.size());
            if (!AppendInput(inventory, "recipe", request.sourceBytes, result.failure) ||
                !AppendInput(inventory, "recipe-meta", request.metaBytes, result.failure) ||
                !AppendInput(inventory, "verified-program", request.verifiedProgramBytes, result.failure))
            {
                return result;
            }
            for (const auto& input : request.codeInputs)
            {
                if (!AppendInput(inventory, input.path, input.bytes, result.failure))
                {
                    return result;
                }
            }
            if (!ComputeSha256(inventory, result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }

        MaterialAssetSetCookResult BuildAuthoredMaterial(const MaterialAssetSetCookRequest& request,
            const Authoring::ReadNode& root)
        {
            MaterialAssetSetCookResult result;
            AuthoredMaterialDocument document;
            document.programAssetId = request.programAssetId;
            if (!ExactMap(root, { "schema", "assetId", "shaderAssetId", "name", "blendMode", "properties",
                "keywords", "keywordSelections" }, result.failure))
            {
                return result;
            }
            if (!DeserializeMaterialAuthoring(root, document.material, result.failure))
            {
                return result;
            }
            if (document.material.assetId != request.materialAssetId || !request.programAssetId.IsValid() ||
                request.verifiedProgramBytes.empty())
            {
                result.failure = "Authored Material requires its sidecar identity, explicit codeProgram and verifiedProgram bundle.";
                return result;
            }
            CodeProgram program;
            std::vector<AssetDependency> edges;
            if (!ReadVerifiedCodeProgram(request.verifiedProgramBytes, program, edges, result.failure) ||
                !CollectCodeProgramDefaultTextures(program.meta, document.defaultTextureAssetIds, result.failure))
            {
                return result;
            }
            if (!request.defaultTextureAssetIds.empty() &&
                !std::ranges::equal(request.defaultTextureAssetIds, document.defaultTextureAssetIds))
            {
                result.failure = "Authored Material provided default texture inventory differs from the verified program.";
                return result;
            }
            if (!CodeInputs(request.codeInputs, program, result.failure) ||
                !ValidateAuthoredMaterialBinding(document, program, result.failure))
            {
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.materialAssetId, {} }, CookedAssetKind::Material };
            if (!EncodeAuthoredMaterialArtifact(document, product.artifactBytes, product.dependencies, result.failure))
            {
                return result;
            }
            product.representation = kAuthoredMaterialRepresentation;
            product.schemaVersion = kAuthoredMaterialVersion;
            product.extension = ".asset";
            if (!InputDigest("authored-material-asset-set-source-v1", request.sourceBytes, request.metaBytes,
                request.verifiedProgramBytes, result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }
    }

    MaterialAssetSetCookResult BuildMaterialAssetSetProduct(const MaterialAssetSetCookRequest& request)
    {
        MaterialAssetSetCookResult result;
        try
        {
            if (request.sourceBytes.empty() || request.sourceBytes.size() > kMaterialDocumentMaxBytes)
            {
                result.failure = "Material source is empty or exceeds its byte budget.";
                return result;
            }
            if (!material_asset_set_producer_detail::ValidateMeta(request.metaBytes, request.materialAssetId,
                result.failure))
            {
                return result;
            }
            const auto source = Authoring::ParsedDocument::ParseText(
                material_asset_set_producer_detail::Text(request.sourceBytes), result.failure);
            if (!source)
            {
                return result;
            }
            if (source.Root().IsMap() && source.Root()["schema"])
            {
                return material_asset_set_producer_detail::BuildAuthoredMaterial(request, source.Root());
            }
            if (!source.Root().IsMap() || !source.Root()["lattice_material"])
            {
                result.failure = "Unsupported Material source shape: this recipe requires a Lattice InstanceDocument; "
                    "authored schema1 requires an explicit codeProgram and verifiedProgram bundle.";
                return result;
            }
            material_graph::InstanceDocument document;
            if (!material_graph::ReadInstanceDocument(source.Root(), document, result.failure))
            {
                return result;
            }
            if (document.materialId != request.materialAssetId)
            {
                result.failure = "Material document identity differs from its captured sidecar.";
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.materialAssetId, {} }, CookedAssetKind::Material };
            if (!EncodeMaterialAssetSetDocument(document, product.artifactBytes, product.dependencies, result.failure))
            {
                return result;
            }
            product.representation = kMaterialDocumentRepresentation;
            product.schemaVersion = kMaterialArtifactVersion;
            product.extension = ".asset";
            if (!material_asset_set_producer_detail::InputDigest("material-asset-set-source-v1", request.sourceBytes,
                request.metaBytes, {}, result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
            return result;
        }
    }

    MaterialAssetSetCookResult BuildMaterialProgramAssetSetProduct(const MaterialProgramAssetSetCookRequest& request)
    {
        MaterialAssetSetCookResult result;
        try
        {
            if (request.sourceBytes.empty() || request.sourceBytes.size() > kMaterialGraphSourceMaxBytes ||
                request.verifiedProgramBytes.empty() ||
                request.verifiedProgramBytes.size() > kMaterialProgramAssetSetMaxBytes)
            {
                result.failure = "MaterialProgram needs bounded graph source and explicit verified program bytes.";
                return result;
            }
            if (!material_asset_set_producer_detail::ValidateMeta(request.metaBytes, request.programAssetId,
                result.failure))
            {
                return result;
            }
            const auto recipe = Authoring::ParsedDocument::ParseText(
                material_asset_set_producer_detail::Text(request.sourceBytes), result.failure);
            if (recipe && recipe.Root().IsMap() && recipe.Root()["code_program"])
            {
                return material_asset_set_producer_detail::BuildCodeProgram(request, recipe.Root());
            }
            result.failure.clear();
            const auto graph = LX::LXMaterialArchive::Read(
                material_asset_set_producer_detail::Text(request.sourceBytes),
                LX::CreateMaterialDefinitions(), &result.failure);
            if (!graph)
            {
                if (result.failure.empty())
                {
                    result.failure = "Cannot decode the current material graph source.";
                }
                return result;
            }
            material_graph::CookedProgram verified;
            if (!material_graph::ReadCookedProgram(
                { reinterpret_cast<const std::uint8_t*>(request.verifiedProgramBytes.data()),
                    request.verifiedProgramBytes.size() }, request.budget, verified, result.failure))
            {
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.programAssetId, {} }, CookedAssetKind::MaterialProgram };
            if (!CollectMaterialProgramAssetSetDependencies(verified.product, request.programAssetId,
                product.dependencies, result.failure))
            {
                return result;
            }
            // Reuse the canonical graph regeneration/equality check and byte
            // writer only. Its legacy untyped manifest edges are never used.
            MaterialProgramCookProduct regenerated;
            if (!BuildMaterialProgramCookProduct(request.programAssetId, *graph, verified.product, request.budget,
                regenerated, result.failure))
            {
                return result;
            }
            if (regenerated.artifactBytes.empty() || regenerated.artifactBytes.size() > kMaterialProgramAssetSetMaxBytes)
            {
                result.failure = "MaterialProgram output exceeds its byte budget.";
                return result;
            }
            product.artifactBytes = std::move(regenerated.artifactBytes);
            product.representation = kMaterialProgramRepresentation;
            product.schemaVersion = kMaterialProgramArtifactVersion;
            product.extension = ".lxmaterial";
            if (!material_asset_set_producer_detail::InputDigest("material-program-asset-set-source-v1",
                request.sourceBytes, request.metaBytes, request.verifiedProgramBytes,
                result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
            return result;
        }
    }
}
