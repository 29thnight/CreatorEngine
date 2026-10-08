#include "MaterialGraphRuntime.h"
#include "MaterialGraphShaderMeta.h"
#include "MaterialPropertyPacker.h"
#include "Assets/AssetIdentityProfile.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace material_graph
{
    namespace
    {
        namespace ck = experiment::cooked;

        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        bool ParseTextureId(std::string_view text, experiment::AssetId& id)
        {
            return experiment::TryParseCanonicalAssetId(text, id) || assets::TryParseCanonicalUuidV8(text, id.value);
        }

        bool ValidTextureId(const experiment::AssetId& id)
        {
            return experiment::IsAssetIdV4(id) || assets::IsUuidV8(id.value);
        }

        bool ValidateDescription(const InstanceDescription& description, std::string& error)
        {
            if (!ValidTextureId(description.graphId) || description.parameters.size() > 128 ||
                description.textures.size() > 64)
                return Fail(error, "Invalid graph identity or oversized material instance.");
            std::set<LX::Id> ids;
            for (const auto& parameter : description.parameters)
            {
                if (parameter.id == 0 || !ids.insert(parameter.id).second)
                    return Fail(error, "Invalid or duplicate material parameter ID.");
                const bool valid = std::visit(
                    [](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        const auto finite = [](double number) {
                            return std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max();
                        };
                        if constexpr (std::is_same_v<T, bool>)
                            return true;
                        else if constexpr (std::is_same_v<T, std::int64_t>)
                            return value >= INT32_MIN && value <= INT32_MAX;
                        else if constexpr (std::is_same_v<T, double>)
                            return finite(value);
                        else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                            return std::ranges::all_of(value, finite);
                        else
                            return false;
                    },
                    parameter.value);
                if (!valid)
                    return Fail(error, "Material overrides require finite float32/int32-compatible typed values.");
            }
            for (const auto& texture : description.textures)
                if (texture.parameter == 0 || !ids.insert(texture.parameter).second || !ValidTextureId(texture.assetId))
                    return Fail(error, "Invalid or duplicate material texture parameter ID/GUID.");
            return true;
        }

        bool Keys(const Authoring::ReadNode& node, std::initializer_list<std::string_view> allowed)
        {
            if (!node.IsMap() || node.Size() != allowed.size())
                return false;
            std::set<std::string_view> seen;
            for (const auto entry : node.Map())
            {
                const auto key = entry.key.Scalar();
                if (std::ranges::find(allowed, key) == allowed.end() || !seen.insert(key).second)
                    return false;
            }
            return true;
        }

        template<typename T>
        T Scalar(const Authoring::ReadNode& node)
        {
            if (!node.IsScalar())
                throw std::runtime_error("Expected material scalar.");
            if constexpr (std::is_same_v<T, std::string>)
                return node.AsString();
            else
                return node.As<T>();
        }

        void WriteDouble(Authoring::WriteNode node, double value)
        {
            std::array<char, 64> buffer;
            const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                                 std::chars_format::general, std::numeric_limits<double>::max_digits10);
            node.SetScalar(std::string_view(buffer.data(), converted.ptr));
        }
    } // namespace

    namespace
    {
        // Capacity-based CPU payload accounting. Small-string inline capacity is
        // deliberately overcharged; STL/allocator and owner control-block overhead
        // are not physical allocation telemetry. Shared shader payloads are charged
        // in full to each retained generation rather than hidden behind a pointer.
        class PayloadCharge
        {
        public:
            void Add(std::size_t bytes) noexcept
            {
                bytes_ = bytes > SIZE_MAX - bytes_ ? SIZE_MAX : bytes_ + bytes;
            }
            void AddString(const std::string& value) noexcept
            {
                Add(value.capacity());
                Add(1);
            }
            void AddPath(const std::filesystem::path& value) noexcept
            {
                const auto capacity = value.native().capacity();
                Add(capacity > SIZE_MAX / sizeof(std::filesystem::path::value_type)
                        ? SIZE_MAX : capacity * sizeof(std::filesystem::path::value_type));
            }
            template<class T>
            void AddVector(const std::vector<T>& values) noexcept
            {
                Add(values.capacity() > SIZE_MAX / sizeof(T) ? SIZE_MAX : values.capacity() * sizeof(T));
            }
            void AddParameter(const LX::LXMaterialParameter& value) noexcept
            {
                AddString(value.identifier);
                AddString(value.name);
                if (const auto* text = std::get_if<std::string>(&value.value))
                {
                    AddString(*text);
                }
            }
            void AddSource(const LX::LXMaterialSource& value) noexcept
            {
                AddVector(value.instances);
                AddString(value.property);
            }
            void AddResource(const LX::LXMaterialResource& value) noexcept
            {
                AddString(value.reference);
                AddSource(value.source);
            }
            std::size_t Bytes() const noexcept { return bytes_; }

        private:
            std::size_t bytes_{};
        };
    }

    std::size_t Generation::RetainedPayloadBytes() const noexcept
    {
        PayloadCharge charge;
        charge.Add(sizeof(Generation));
        charge.AddString(cooked.metadata);
        charge.AddString(cooked.boundSource);
        const auto& product = cooked.product;
        const auto& program = product.program;
        charge.AddString(program.slang);
        charge.AddString(program.semanticKey);
        charge.AddVector(program.parameters);
        for (const auto& parameter : program.parameters)
        {
            charge.AddParameter(parameter);
        }
        charge.AddVector(program.resources);
        for (const auto& resource : program.resources)
        {
            charge.AddResource(resource);
        }
        charge.AddVector(program.sourceMap);
        for (const auto& range : program.sourceMap)
        {
            charge.AddSource(range.source);
        }
        charge.AddString(product.selection.reason);
        charge.AddVector(product.layout.parameters);
        for (const auto& binding : product.layout.parameters)
        {
            charge.AddParameter(binding.parameter);
        }
        charge.AddVector(product.layout.textures);
        for (const auto& resource : product.layout.textures)
        {
            charge.AddResource(resource);
        }
        charge.AddVector(product.layout.samplers);
        for (const auto& resource : product.layout.samplers)
        {
            charge.AddResource(resource);
        }
        charge.AddVector(product.shaders);
        for (const auto& shader : product.shaders)
        {
            charge.AddString(shader.backend);
            charge.AddString(shader.entryPoint);
            charge.AddVector(shader.bytecode);
        }
        charge.AddVector(product.targets);
        for (const auto& target : product.targets)
        {
            charge.AddString(target.entry);
            charge.AddString(target.profile);
        }
        charge.AddVector(product.dependencies);
        for (const auto& dependency : product.dependencies)
        {
            charge.AddPath(dependency);
        }
        if (product.materialShader)
        {
            const auto& shader = *product.materialShader;
            charge.Add(sizeof(GeneratedMaterialShader));
            charge.AddPath(shader.metaPath);
            charge.AddString(shader.document);
            charge.AddString(shader.source);
            charge.AddVector(shader.cookedContract);
            const auto& meta = shader.meta;
            charge.AddString(meta.name);
            charge.AddPath(meta.source);
            charge.AddPath(meta.originPath);
            charge.AddVector(meta.properties);
            for (const auto& property : meta.properties)
            {
                charge.AddString(property.name);
                charge.AddString(property.label);
                charge.AddString(property.semantic);
                charge.AddString(property.colorSpace);
            }
            charge.AddVector(meta.keywords);
            for (const auto& keyword : meta.keywords)
            {
                charge.AddString(keyword.name);
                charge.AddVector(keyword.values);
                for (const auto& value : keyword.values)
                {
                    charge.AddString(value);
                }
            }
            charge.AddVector(meta.passes);
            for (const auto& pass : meta.passes)
            {
                charge.AddString(pass.name);
                if (pass.vertex)
                {
                    charge.AddString(pass.vertex->entry);
                }
                if (pass.pixel)
                {
                    charge.AddString(pass.pixel->entry);
                }
                if (pass.compute)
                {
                    charge.AddString(pass.compute->entry);
                }
            }
            if (meta.generatedMaterial)
            {
                charge.AddString(meta.generatedMaterial->generation);
                charge.AddString(meta.generatedMaterial->sourceSha256);
                charge.AddVector(meta.generatedMaterial->samplers);
                for (const auto& sampler : meta.generatedMaterial->samplers)
                {
                    charge.AddString(sampler.name);
                    charge.AddString(sampler.description);
                }
            }
            charge.AddString(shader.layout.constantBufferName);
            charge.AddVector(shader.layout.properties);
            for (const auto& property : shader.layout.properties)
            {
                charge.AddString(property.name);
                charge.AddString(property.resourceName);
            }
            charge.AddVector(shader.layout.samplers);
            for (const auto& sampler : shader.layout.samplers)
            {
                charge.AddString(sampler.name);
                charge.AddVector(sampler.fields);
                for (const auto& field : sampler.fields)
                {
                    charge.AddString(field.name);
                }
            }
        }
        return charge.Bytes();
    }

    struct GenerationPreparationState
    {
        experiment::AssetId m_assetId;
        std::uint64_t m_generation{};
        std::mutex m_mutex;
        bool m_attempted{};
        std::atomic<bool> m_failed{};
        own::shared_owner<const Generation> m_owner;
        ck::Sha256Digest m_digest{};
        own::shared_owner<const Generation> m_previousOwner;
        ck::Sha256Digest m_previousDigest{};
        std::string m_error;
    };

    PreparedGeneration::PreparedGeneration(ConstructionKey, own::shared_owner<GenerationPreparationState> state,
                                           own::shared_owner<const Generation> owner, ck::Sha256Digest digest,
                                           std::uint64_t requestGeneration)
        : m_state(std::move(state)), m_owner(std::move(owner)), m_digest(digest),
          m_requestGeneration(requestGeneration)
    {
    }

    std::uint64_t GenerationStore::NextUseLocked() const noexcept
    {
        if (useSerial_ != UINT64_MAX)
        {
            ++useSerial_;
        }
        return useSerial_;
    }

    void GenerationStore::TrimRetainedLocked(std::vector<own::shared_owner<const Generation>>& released)
    {
        while (retainedBytes_ > retainedBudgetBytes_)
        {
            auto oldest = entries_.end();
            for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
            {
                if (entry->second.retained &&
                    (oldest == entries_.end() || entry->second.lastUse < oldest->second.lastUse))
                {
                    oldest = entry;
                }
            }
            if (oldest == entries_.end())
            {
                break;
            }
            released.push_back(std::move(oldest->second.retained));
            retainedBytes_ -= oldest->second.retainedBytes;
            oldest->second.retainedBytes = 0;
        }
    }

    void GenerationStore::RetainLocked(Entry& entry, const own::shared_owner<const Generation>& owner,
                                      std::vector<own::shared_owner<const Generation>>& released)
    {
        entry.lastUse = NextUseLocked();
        if (entry.retained)
        {
            released.push_back(std::move(entry.retained));
            retainedBytes_ -= entry.retainedBytes;
            entry.retainedBytes = 0;
        }
        const auto bytes = owner->RetainedPayloadBytes();
        if (bytes <= retainedBudgetBytes_ && bytes <= SIZE_MAX - retainedBytes_)
        {
            entry.retained = owner;
            entry.retainedBytes = bytes;
            retainedBytes_ += bytes;
        }
        TrimRetainedLocked(released);
    }

    GenerationPreparationRequest GenerationStore::BeginPreparation(const experiment::AssetId& id, bool reload,
                                                                   std::string& error)
    {
        error.clear();
        if (!ValidTextureId(id))
        {
            error = "Material graph generation requires a canonical UUIDv4/UUIDv8.";
            return {};
        }
        GenerationPreparationRequest result;
        std::vector<own::shared_owner<const Generation>> released;
        own::shared_owner<GenerationPreparationState> candidate;
        own::shared_owner<GenerationPreparationState> pending;
        own::shared_owner<const Generation> current;
        {
            std::lock_guard lock(mutex_);
            auto& entry = entries_[id];
            current = entry.current.lock();
            pending = entry.request.lock();
            if (current && (current->assetId != id || current->generation != entry.currentGeneration))
            {
                current.reset();
            }
            if (!reload && current && !entry.dirty)
            {
                // At most the replaced retained owner plus every entry can be
                // retired below; allocate before any cache accounting changes.
                released.reserve(entries_.size() + 1);
                result.m_cachedOwner = current;
                result.m_cachedDigest = entry.digest;
                result.m_requestGeneration = entry.requestGeneration;
                RetainLocked(entry, current, released);
            }
            else if (!reload && pending && pending->m_generation == entry.requestGeneration &&
                     !pending->m_failed.load(std::memory_order_acquire))
            {
                result.m_requestGeneration = entry.requestGeneration;
                result.m_state = std::move(pending);
            }
            else
            {
                if (serial_ == UINT64_MAX)
                {
                    error = "Material generation counter is exhausted.";
                    return {};
                }
                candidate = own::make_shared<GenerationPreparationState>();
                candidate->m_assetId = id;
                candidate->m_generation = ++serial_;
                candidate->m_previousOwner = std::move(current);
                candidate->m_previousDigest = entry.digest;
                entry.request = candidate;
                entry.requestGeneration = candidate->m_generation;
                entry.dirty = true;
                result.m_requestGeneration = entry.requestGeneration;
                result.m_state = std::move(candidate);
            }
        }
        return result;
    }

    own::shared_owner<const PreparedGeneration> GenerationStore::Prepare(const GenerationPreparationRequest& request,
                                                                        const GenerationLoader& loader,
                                                                        std::string& error)
    {
        error.clear();
        if (request.m_cachedOwner)
        {
            return own::make_shared<const PreparedGeneration>(PreparedGeneration::ConstructionKey{},
                request.m_state, request.m_cachedOwner, request.m_cachedDigest, request.m_requestGeneration);
        }
        if (!request.m_state)
        {
            error = "Material generation preparation requires a valid request.";
            return {};
        }
        if (!loader)
        {
            error = "Material graph generation requires a loader.";
            return {};
        }
        auto& state = *request.m_state;
        // Same-ticket loaders are coalesced, without holding the store mutex.
        std::lock_guard lock(state.m_mutex);
        if (!state.m_attempted)
        {
            state.m_attempted = true;
            try
            {
                Generation generation;
                generation.assetId = state.m_assetId;
                generation.generation = state.m_generation;
                std::vector<std::uint8_t> payload;
                bool verified = false;
                {
                    auto& loaded = generation.cooked;
                    if (!loader(loaded, state.m_error))
                    {
                        if (state.m_error.empty())
                        {
                            state.m_error = "Material generation loader failed.";
                        }
                    }
                    else if (!WriteCookedProgram(loaded.product, {}, payload, state.m_error) ||
                             (loaded.product.materialShader &&
                              loaded.product.materialShader->meta.guid.m_guid != state.m_assetId.value) ||
                             loaded.boundSource != BuildBoundSource(loaded.product.program) ||
                             loaded.metadata != LX::WriteMaterialProgramMetadata(loaded.product.program))
                    {
                        if (state.m_error.empty())
                        {
                            state.m_error = "Material generation metadata differs from its verified product.";
                        }
                    }
                    else
                    {
                        verified = ck::ComputeSha256(std::as_bytes(std::span(payload)), state.m_digest, state.m_error);
                    }
                }
                if (verified)
                {
                    if (state.m_previousOwner && state.m_previousDigest == state.m_digest)
                    {
                        state.m_owner = state.m_previousOwner;
                    }
                    else
                    {
                        // No shared mutable build owner or borrowed build alias
                        // survives this completed-value const publication.
                        state.m_owner = own::make_shared<const Generation>(std::move(generation));
                    }
                    state.m_error.clear();
                }
            }
            catch (const std::exception& exception)
            {
                state.m_error = std::string("Material generation preparation failed: ") + exception.what();
            }
            catch (...)
            {
                state.m_error = "Material generation preparation failed with an unknown exception.";
            }
            state.m_previousOwner.reset();
            state.m_failed.store(!state.m_owner, std::memory_order_release);
        }
        error = state.m_error;
        if (!state.m_owner)
        {
            return {};
        }
        return own::make_shared<const PreparedGeneration>(PreparedGeneration::ConstructionKey{},
            request.m_state, state.m_owner, state.m_digest, request.m_requestGeneration);
    }

    own::shared_owner<const Generation> GenerationStore::Publish(const PreparedGeneration& prepared, std::string& error)
    {
        error.clear();
        std::vector<own::shared_owner<const Generation>> released;
        own::shared_owner<const Generation> result;
        own::shared_owner<const Generation> current;
        {
            std::lock_guard lock(mutex_);
            const auto found = entries_.find(prepared.m_owner->assetId);
            if (found == entries_.end() || found->second.requestGeneration != prepared.m_requestGeneration)
            {
                error = "Material generation preparation was superseded or invalidated before publication.";
                return {};
            }
            // Publication below is non-allocating. Allocation failure must not
            // update Current, request acceptance or retained-byte accounting.
            released.reserve(entries_.size() + 1);
            auto& entry = found->second;
            current = entry.current.lock();
            if (current && current->generation == entry.currentGeneration && entry.digest == prepared.m_digest)
            {
                result = std::move(current);
            }
            else
            {
                result = prepared.m_owner;
            }
            entry.current = result;
            entry.currentGeneration = result->generation;
            entry.digest = prepared.m_digest;
            entry.dirty = false;
            // The index never strongly owns a completed request. Prepared tickets
            // and workers retain private state only while their work needs it.
            entry.request.reset();
            RetainLocked(entry, result, released);
        }
        return result;
    }

    own::shared_owner<const Generation> GenerationStore::Load(const experiment::AssetId& id,
                                                              const GenerationLoader& loader, bool reload,
                                                              std::string& error)
    {
        if (!loader)
        {
            error = "Material graph generation requires a loader.";
            return {};
        }
        const auto request = BeginPreparation(id, reload, error);
        if (!request)
        {
            return {};
        }
        if (request.m_cachedOwner)
        {
            // A captured read remains valid if another thread reloads afterwards.
            return request.m_cachedOwner;
        }
        const auto prepared = Prepare(request, loader, error);
        if (!prepared)
        {
            return {};
        }
        return Publish(*prepared, error);
    }

    own::shared_owner<const Generation> GenerationStore::Current(const experiment::AssetId& id) const
    {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(id);
        if (found == entries_.end())
        {
            return {};
        }
        auto current = found->second.current.lock();
        if (!current || current->assetId != id || current->generation != found->second.currentGeneration)
        {
            return {};
        }
        found->second.lastUse = NextUseLocked();
        return current;
    }

    void GenerationStore::InvalidatePreparation(const experiment::AssetId& id)
    {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(id);
        if (found != entries_.end())
        {
            found->second.dirty = true;
            found->second.requestGeneration = 0;
            found->second.request.reset();
        }
    }

    void GenerationStore::Remove(const experiment::AssetId& id)
    {
        decltype(entries_)::node_type removed;
        {
            std::lock_guard lock(mutex_);
            removed = entries_.extract(id);
            if (!removed.empty())
            {
                retainedBytes_ -= removed.mapped().retainedBytes;
            }
        }
    }

    void GenerationStore::Clear()
    {
        decltype(entries_) removed;
        {
            std::lock_guard lock(mutex_);
            removed.swap(entries_);
            retainedBytes_ = 0;
        }
    }

    void GenerationStore::SetRetainedBudgetBytes(std::size_t bytes)
    {
        std::vector<own::shared_owner<const Generation>> released;
        {
            std::lock_guard lock(mutex_);
            released.reserve(entries_.size());
            retainedBudgetBytes_ = bytes;
            TrimRetainedLocked(released);
        }
    }

    std::size_t GenerationStore::RetainedBytes() const
    {
        std::lock_guard lock(mutex_);
        return retainedBytes_;
    }

    std::size_t GenerationStore::RetainedBudgetBytes() const
    {
        std::lock_guard lock(mutex_);
        return retainedBudgetBytes_;
    }

    bool LoadCookedGeneration(const ck::CookedAssetCatalog& catalog, const ck::ArtifactByteSource& bytes,
                              const experiment::AssetId& id, const LX::LXMaterialAsset* source, CookedProgram& result,
                              std::string& error)
    {
        CookedProgram candidate;
        if (!catalog.OpenMaterialProgram(id, bytes, {}, candidate, error))
            return false;
        if (source)
        {
            const auto generated = LX::GenerateMaterialSlang(*source);
            if (!generated || generated->slang != candidate.product.program.slang ||
                LX::WriteMaterialProgramMetadata(*generated) != candidate.metadata)
                return Fail(error, "Cooked material generation differs from the current source graph; regenerate it.");
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool BuildInstance(own::shared_owner<const Generation> generation, const InstanceDescription& description,
                       const TextureLoader& loadTexture, own::shared_owner<const Instance>& result, std::string& error)
    {
        error.clear();
        if (!generation || generation->generation == 0 || generation->assetId != description.graphId)
        {
            return Fail(error, "Material instance requires its matching owning graph generation.");
        }
        if (!ValidateDescription(description, error))
        {
            return false;
        }
        Instance candidate;
        candidate.generation = std::move(generation);
        candidate.description = description;
        std::ranges::sort(candidate.description.parameters, {}, &ParameterOverride::id);
        std::ranges::sort(candidate.description.textures, {}, &TextureOverride::parameter);
        const auto& product = candidate.generation->cooked.product;
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        if (product.materialShader)
        {
            const auto& shader = *product.materialShader;
            const auto& meta = shader.meta;
            for (const auto& edit : description.parameters)
            {
                const auto property = std::ranges::find(meta.properties, edit.id, &ShaderPropertyDesc::parameterId);
                if (property == meta.properties.end() || !property->exposed || property->type == ShaderPropertyType::Texture2D)
                {
                    return Fail(error, "Unknown or private generated material numeric override.");
                }
            }
            for (const auto& property : meta.properties)
            {
                MaterialPropertyValue value;
                if (!MaterialPropertyPacker::ApplyDefault(property, value, error))
                {
                    return false;
                }
                const auto edit = std::ranges::find(description.parameters, property.parameterId, &ParameterOverride::id);
                if (edit != description.parameters.end())
                {
                    const auto parameter = std::ranges::find(product.program.parameters, edit->id, &LX::LXMaterialParameter::id);
                    if (parameter == product.program.parameters.end() || !LX::IsSocketValueValid(parameter->type, edit->value))
                    {
                        return Fail(error, "Generated material override type differs from its Blackboard type.");
                    }
                    std::visit([&](const auto& input) {
                        using T = std::decay_t<decltype(input)>;
                        if constexpr (std::is_same_v<T, bool>)
                        {
                            value.m_boolValue = input;
                        }
                        else if constexpr (std::is_same_v<T, std::int64_t>)
                        {
                            value.m_integerValue = static_cast<std::int32_t>(input);
                        }
                        else if constexpr (std::is_same_v<T, double>)
                        {
                            value.m_numericValue = {static_cast<float>(input)};
                        }
                        else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                        {
                            value.m_numericValue.clear();
                            for (const auto number : input)
                            {
                                value.m_numericValue.push_back(static_cast<float>(number));
                            }
                        }
                    }, edit->value);
                }
                if (property.type == ShaderPropertyType::Texture2D)
                {
                    const auto texture = std::ranges::find(description.textures, property.parameterId, &TextureOverride::parameter);
                    if (texture != description.textures.end())
                    {
                        value.m_textureGuid = FileGuid{texture->assetId.value};
                    }
                }
                candidate.properties.push_back(std::move(value));
            }
        }
        else if (!PrepareUniforms(product.layout, description.parameters, candidate.uniforms, diagnostics))
        {
            return Fail(error, diagnostics.empty() ? "Invalid material instance uniforms." : diagnostics.front().message);
        }
        for (const auto& texture : description.textures)
        {
            const auto parameter =
                std::ranges::find(product.program.parameters, texture.parameter, &LX::LXMaterialParameter::id);
            if (parameter == product.program.parameters.end() || !parameter->exposed ||
                parameter->type != LX::PinType::Texture)
            {
                return Fail(error, "Unknown or private material texture parameter override.");
            }
            if (std::ranges::none_of(product.layout.textures,
                                     [&](const auto& resource) { return resource.parameter == texture.parameter; }))
            {
                return Fail(error, "Material texture parameter has no active resource binding.");
            }
        }
        for (const auto& resource : product.layout.textures)
        {
            experiment::AssetId id;
            const auto override = std::ranges::find(description.textures, resource.parameter, &TextureOverride::parameter);
            if (override != description.textures.end())
            {
                id = override->assetId;
            }
            else if (!ParseTextureId(resource.reference, id))
            {
                return Fail(error, "Graph texture resource requires a canonical asset GUID.");
            }
            if (!loadTexture)
            {
                return Fail(error, "Material instance has no texture generation loader.");
            }
            auto owner = loadTexture(id, resource.colorSpace, error);
            if (!owner)
            {
                if (error.empty())
                {
                    error = "Material texture generation could not be loaded: " + Uuid::ToString(id.value);
                }
                return false;
            }
            candidate.textures.push_back({resource.slot, id, resource.colorSpace, std::move(owner)});
            if (product.materialShader)
            {
                candidate.textureOwners.push_back({"lx_texture_" + std::to_string(resource.slot), candidate.textures.back().owner});
            }
        }
        if (product.materialShader)
        {
            own::shared_owner<const LX::Runtime::Instance> common;
            if (!LX::Runtime::BuildInstance(product.materialShader, candidate.properties, {}, candidate.textureOwners,
                                             common, error))
            {
                return false;
            }
            static_cast<LX::Runtime::Instance&>(candidate) = *common;
        }
        result = own::make_shared<const Instance>(std::move(candidate));
        return true;
    }

    bool WriteInstanceDocument(const InstanceDocument& document, Authoring::WriteNode result, std::string& error)
    {
        error.clear();
        if (!result || document.name.size() > 4096 ||
            (document.blendMode != "opaque" && document.blendMode != "masked" && document.blendMode != "transparent") ||
            (document.materialId.IsValid() && !ValidTextureId(document.materialId)) ||
            !ValidateDescription(document.description, error))
            return Fail(error, error.empty() ? "Invalid lattice material document." : error);
        Authoring::WriteDocument staging;
        const auto node = staging.Root();
        node.SetMap();
        node.Child("lattice_material").SetScalar(1u);
        node.Child("name").SetScalar(document.name);
        node.Child("assetId").SetScalar(Uuid::ToString(document.materialId.value));
        node.Child("graphAssetId").SetScalar(Uuid::ToString(document.description.graphId.value));
        node.Child("doubleSided").SetScalar(document.doubleSided);
        node.Child("blendMode").SetScalar(document.blendMode);
        auto parameters = document.description.parameters;
        std::ranges::sort(parameters, {}, &ParameterOverride::id);
        const auto list = node.Child("parameters");
        list.SetSequence();
        for (const auto& parameter : parameters)
        {
            const auto entry = list.Append();
            entry.Child("id").SetScalar(parameter.id);
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, bool>)
                        entry.Child("bool").SetScalar(value);
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                        entry.Child("int").SetScalar(value);
                    else if constexpr (std::is_same_v<T, double>)
                        WriteDouble(entry.Child("float"), value);
                    else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                    {
                        const auto values = entry.Child(value.size() == 3 ? "vector" : "color");
                        values.SetSequence(true);
                        for (double number : value)
                            WriteDouble(values.Append(), number);
                    }
                },
                parameter.value);
        }
        auto textures = document.description.textures;
        std::ranges::sort(textures, {}, &TextureOverride::parameter);
        const auto textureList = node.Child("textures");
        textureList.SetSequence();
        for (const auto& texture : textures)
        {
            const auto entry = textureList.Append();
            entry.Child("id").SetScalar(texture.parameter);
            entry.Child("guid").SetScalar(Uuid::ToString(texture.assetId.value));
        }
        result.Assign(node);
        return true;
    }

    bool ReadInstanceDocument(const Authoring::ReadNode& node, InstanceDocument& result, std::string& error)
    {
        error.clear();
        try
        {
            const bool keys = node["blendMode"]
                ? Keys(node, {"lattice_material", "name", "assetId", "graphAssetId", "doubleSided", "parameters", "textures", "blendMode"})
                : Keys(node, {"lattice_material", "name", "assetId", "graphAssetId", "doubleSided", "parameters", "textures"});
            if (!keys ||
                Scalar<std::uint32_t>(node["lattice_material"]) != 1)
                return Fail(error, "Unknown, duplicate or incomplete lattice material document keys/version.");
            InstanceDocument candidate;
            candidate.name = Scalar<std::string>(node["name"]);
            const auto materialId = Scalar<std::string>(node["assetId"]);
            if (materialId != Uuid::ToString(Uuid::Uuid16{}) && !ParseTextureId(materialId, candidate.materialId))
                return Fail(error, "Invalid lattice material asset GUID.");
            if (!ParseTextureId(Scalar<std::string>(node["graphAssetId"]), candidate.description.graphId))
                return Fail(error, "Invalid lattice graph asset GUID.");
            candidate.doubleSided = Scalar<bool>(node["doubleSided"]);
            if (node["blendMode"])
            {
                candidate.blendMode = Scalar<std::string>(node["blendMode"]);
                if (candidate.blendMode != "opaque" && candidate.blendMode != "masked" && candidate.blendMode != "transparent")
                    return Fail(error, "Invalid lattice material alpha mode.");
            }
            if (candidate.name.size() > 4096 || !node["parameters"].IsSequence() || node["parameters"].Size() > 128 ||
                !node["textures"].IsSequence() || node["textures"].Size() > 64)
                return Fail(error, "Invalid lattice material parameter/texture list.");
            for (const auto entry : node["parameters"])
            {
                ParameterOverride parameter;
                parameter.id = Scalar<LX::Id>(entry["id"]);
                if (Keys(entry, {"id", "bool"}))
                    parameter.value = Scalar<bool>(entry["bool"]);
                else if (Keys(entry, {"id", "int"}))
                    parameter.value = Scalar<std::int64_t>(entry["int"]);
                else if (Keys(entry, {"id", "float"}))
                    parameter.value = Scalar<double>(entry["float"]);
                else if (Keys(entry, {"id", "vector"}) && entry["vector"].IsSequence() && entry["vector"].Size() == 3)
                    parameter.value =
                        std::array<double, 3>{Scalar<double>(entry["vector"].At(0)), Scalar<double>(entry["vector"].At(1)),
                                              Scalar<double>(entry["vector"].At(2))};
                else if (Keys(entry, {"id", "color"}) && entry["color"].IsSequence() && entry["color"].Size() == 4)
                    parameter.value =
                        std::array<double, 4>{Scalar<double>(entry["color"].At(0)), Scalar<double>(entry["color"].At(1)),
                                              Scalar<double>(entry["color"].At(2)), Scalar<double>(entry["color"].At(3))};
                else
                    return Fail(error, "Material parameter needs exactly one known typed value.");
                candidate.description.parameters.push_back(std::move(parameter));
            }
            for (const auto entry : node["textures"])
            {
                TextureOverride texture;
                if (!Keys(entry, {"id", "guid"}) || !ParseTextureId(Scalar<std::string>(entry["guid"]), texture.assetId))
                    return Fail(error, "Invalid lattice texture override.");
                texture.parameter = Scalar<LX::Id>(entry["id"]);
                candidate.description.textures.push_back(texture);
            }
            if (!ValidateDescription(candidate.description, error))
                return false;
            std::ranges::sort(candidate.description.parameters, {}, &ParameterOverride::id);
            std::ranges::sort(candidate.description.textures, {}, &TextureOverride::parameter);
            result = std::move(candidate);
            return true;
        }
        catch (const std::exception& exception)
        {
            return Fail(error, exception.what());
        }
    }
} // namespace material_graph
