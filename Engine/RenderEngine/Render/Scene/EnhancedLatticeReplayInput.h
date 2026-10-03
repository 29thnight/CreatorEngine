#pragma once
#include "EnhancedDrawReplayInput.h"
#include "../../MaterialGraphRuntime.h"
#include "../../Texture.h"
#include <map>
#include <set>

// Lattice instance slice only. Resolve an exact current immutable program and
// texture closure; store typed parameter IDs/values, never uniform byte offsets.
struct EnhancedLatticeReplayInput
{
    struct TextureIdentity
    {
        uint32_t slot{}, colorSpace{};
        experiment::AssetId assetId;
        uint64_t content{};
        bool operator==(const TextureIdentity&) const = default;
    };
    struct Draw
    {
        std::array<uint8_t,32> geometryIds{};
        uint64_t program{};
        EnhancedMaterialCoverage coverage;
        material_graph::InstanceDescription description;
        std::vector<TextureIdentity> textures;
    };
    std::vector<Draw> draws;
    static constexpr size_t kMaxBytes=16u*1024u*1024u;
    static constexpr std::array<uint8_t,8> kMagic{'C','E','L','X','I','0','0','1'};

    struct Writer
    {
        std::vector<uint8_t> bytes;
        void Number(uint64_t value, unsigned count=4)
        { for(unsigned i=0;i<count;++i) bytes.push_back(static_cast<uint8_t>(value>>(8*i))); }
        void Block(std::span<const uint8_t> value) { bytes.insert(bytes.end(),value.begin(),value.end()); }
        void Text(const std::string& value)
        { Number(value.size(),8); Block({reinterpret_cast<const uint8_t*>(value.data()),value.size()}); }
        void Float(float value) { Number(std::bit_cast<uint32_t>(value)); }
        void Double(double value) { Number(std::bit_cast<uint64_t>(value),8); }
        void Value(const LX::LXSocketValue& value)
        {
            std::visit([&](const auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,bool>) { Number(0); Number(v ? 1 : 0,8); }
                else if constexpr(std::is_same_v<T,int64_t>) { Number(1); Number(std::bit_cast<uint64_t>(v),8); }
                else if constexpr(std::is_same_v<T,double>) { Number(2); Double(v); }
                else if constexpr(std::is_same_v<T,std::array<double,3>>) { Number(3); for(auto n:v) Double(n); }
                else if constexpr(std::is_same_v<T,std::array<double,4>>) { Number(4); for(auto n:v) Double(n); }
                else throw std::runtime_error("unsupported Lattice replay value");
            },value);
        }
    };
    struct Reader
    {
        std::span<const uint8_t> bytes;
        size_t offset{};
        void Need(size_t count) const
        { if(count>bytes.size()-offset) throw std::runtime_error("Lattice replay payload truncated"); }
        uint64_t Number(unsigned count=4)
        { Need(count); uint64_t v{}; for(unsigned i=0;i<count;++i) v|=uint64_t(bytes[offset++])<<(8*i); return v; }
        void Block(std::span<uint8_t> output)
        { Need(output.size()); std::copy_n(bytes.begin()+offset,output.size(),output.begin()); offset+=output.size(); }
        float Float()
        { auto v=std::bit_cast<float>(static_cast<uint32_t>(Number())); if(!std::isfinite(v)) throw std::runtime_error("nonfinite Lattice replay float"); return v; }
        double Double()
        { auto v=std::bit_cast<double>(Number(8)); if(!std::isfinite(v) || std::abs(v)>std::numeric_limits<float>::max()) throw std::runtime_error("invalid Lattice replay numeric value"); return v; }
        LX::LXSocketValue Value()
        {
            switch(Number())
            {
            case 0: { auto v=Number(8); if(v>1) throw std::runtime_error("invalid Lattice replay bool"); return v!=0; }
            case 1: { auto v=std::bit_cast<int64_t>(Number(8)); if(v<INT32_MIN || v>INT32_MAX) throw std::runtime_error("invalid Lattice replay int"); return v; }
            case 2: return Double();
            case 3: return std::array<double,3>{Double(),Double(),Double()};
            case 4: return std::array<double,4>{Double(),Double(),Double(),Double()};
            default: throw std::runtime_error("unknown Lattice replay value type");
            }
        }
        uint32_t Count(uint32_t maximum)
        { auto n=Number(); if(n>maximum) throw std::runtime_error("Lattice replay count exceeds budget"); return static_cast<uint32_t>(n); }
        experiment::AssetId Asset()
        {
            experiment::AssetId id; Block(id.value.data);
            if(!experiment::IsAssetIdV4(id) && !assets::IsUuidV8(id.value)) throw std::runtime_error("invalid Lattice replay asset ID");
            return id;
        }
    };
    static uint64_t ProgramDigest(const material_graph::Generation& generation)
    {
        Writer w;
        const auto& product=generation.cooked.product;
        w.Text(generation.cooked.metadata); w.Text(generation.cooked.boundSource);
        w.Text(LX::WriteMaterialProgramMetadata(product.program)); w.Text(product.program.semanticKey);
        w.Number(static_cast<uint32_t>(product.selection.tier)); w.Number(static_cast<uint32_t>(product.selection.route));
        w.Number(product.layout.uniformBytes); w.Number(product.layout.parameters.size());
        for(const auto& p:product.layout.parameters) { w.Number(p.parameter.id,8); w.Number(p.offset); w.Number(p.bytes); }
        w.Number(product.shaders.size());
        for(const auto& s:product.shaders) { w.Text(s.backend); w.Text(s.entryPoint); w.Number(s.bytecode.size(),8); w.Block(s.bytecode); }
        return EnhancedCameraReplayInput::Checksum(w.bytes);
    }
    static uint64_t TextureDigest(const Texture& texture)
    {
        const auto view=texture.GetImageView();
        if(view.IsEmpty()) throw std::runtime_error("Lattice replay texture lacks CPU content owner");
        uint64_t hash=14695981039346656037ull;
        const auto mix=[&](std::span<const uint8_t> bytes) { for(auto b:bytes) { hash^=b; hash*=1099511628211ull; } };
        Writer w;
        w.Number(static_cast<uint32_t>(view.Format())); w.Number(view.Width()); w.Number(view.Height());
        w.Number(view.MipLevels()); w.Number(view.ArraySize()); w.Number(view.IsCube()); w.Number(view.SubresourceCount());
        mix(w.bytes);
        for(uint32_t i=0;i<view.SubresourceCount();++i)
        {
            const auto* image=view.At(i);
            if(!image || !image->pixels || !image->slicePitch) throw std::runtime_error("Lattice replay texture subresource missing");
            Writer info; info.Number(image->width); info.Number(image->height); info.Number(image->rowPitch,8); info.Number(image->slicePitch,8);
            mix(info.bytes); mix({reinterpret_cast<const uint8_t*>(image->pixels),image->slicePitch});
        }
        return hash;
    }
    std::vector<uint8_t> Encode() const
    {
        Writer w; w.Block(kMagic); w.Number(1); w.Number(draws.size());
        for(const auto& d:draws)
        {
            w.Block(d.geometryIds); w.Block(d.description.graphId.value.data); w.Number(d.program,8);
            w.Number(d.coverage.flags); w.Float(d.coverage.cutoff); w.Float(d.coverage.baseAlpha);
            w.Number(d.description.parameters.size());
            for(const auto& p:d.description.parameters) { w.Number(p.id,8); w.Value(p.value); }
            w.Number(d.description.textures.size());
            for(const auto& t:d.description.textures) { w.Number(t.parameter,8); w.Block(t.assetId.value.data); }
            w.Number(d.textures.size());
            for(const auto& t:d.textures) { w.Number(t.slot); w.Number(t.colorSpace); w.Block(t.assetId.value.data); w.Number(t.content,8); }
        }
        w.Number(EnhancedCameraReplayInput::Checksum(w.bytes),8); return std::move(w.bytes);
    }
    static bool Decode(std::span<const uint8_t> bytes, EnhancedLatticeReplayInput& output, std::string& error)
    {
        try
        {
            if(bytes.size()<24 || bytes.size()>kMaxBytes || !std::equal(kMagic.begin(),kMagic.end(),bytes.begin()))
                throw std::runtime_error("Lattice replay size/magic mismatch");
            Reader checksum{bytes.last(8)};
            if(checksum.Number(8)!=EnhancedCameraReplayInput::Checksum(bytes.first(bytes.size()-8)))
                throw std::runtime_error("Lattice replay checksum mismatch");
            Reader r{bytes.first(bytes.size()-8),8};
            if(r.Number()!=1) throw std::runtime_error("unsupported Lattice replay version");
            const auto count=r.Count(4096);
            r.Need(size_t(count)*80); // Minimum record; checked again before each variable read.
            EnhancedLatticeReplayInput candidate;
            for(uint32_t i=0;i<count;++i)
            {
                Draw d; r.Block(d.geometryIds); d.description.graphId=r.Asset(); d.program=r.Number(8);
                d.coverage.flags=static_cast<uint32_t>(r.Number()); d.coverage.cutoff=r.Float(); d.coverage.baseAlpha=r.Float();
                if(!d.coverage.IsValid() || (d.coverage.flags&EnhancedMaterialCoverage::Blended))
                    throw std::runtime_error("unsupported Lattice replay coverage");
                std::set<LX::Id> ids;
                for(uint32_t j=0,n=r.Count(128);j<n;++j)
                {
                    const auto id=r.Number(8);
                    if(!id || !ids.insert(id).second) throw std::runtime_error("duplicate/zero Lattice replay parameter ID");
                    d.description.parameters.push_back({id,r.Value()});
                }
                for(uint32_t j=0,n=r.Count(64);j<n;++j)
                {
                    const auto id=r.Number(8);
                    if(!id || !ids.insert(id).second) throw std::runtime_error("duplicate/zero Lattice replay texture parameter ID");
                    d.description.textures.push_back({id,r.Asset()});
                }
                std::set<uint32_t> slots;
                for(uint32_t j=0,n=r.Count(64);j<n;++j)
                {
                    TextureIdentity t; t.slot=static_cast<uint32_t>(r.Number()); t.colorSpace=static_cast<uint32_t>(r.Number());
                    if(t.slot>=64 || t.colorSpace>2 || !slots.insert(t.slot).second) throw std::runtime_error("invalid Lattice replay texture binding");
                    t.assetId=r.Asset(); t.content=r.Number(8); d.textures.push_back(t);
                }
                candidate.draws.push_back(std::move(d));
            }
            if(r.offset!=r.bytes.size()) throw std::runtime_error("Lattice replay trailing payload");
            output=std::move(candidate); error.clear(); return true;
        }
        catch(const std::exception& e) { error=e.what(); return false; }
    }
    static bool Load(const std::string& path, EnhancedLatticeReplayInput& output, std::string& error)
    {
        std::error_code ec; const auto file=std::filesystem::path(path); const auto size=std::filesystem::file_size(file,ec);
        if(!file.is_absolute() || ec || size<24 || size>kMaxBytes) { error="Lattice replay requires bounded absolute file"; return false; }
        std::vector<uint8_t> bytes(static_cast<size_t>(size)); std::ifstream stream(file,std::ios::binary);
        if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || stream.peek()!=EOF) { error="Lattice replay read failed"; return false; }
        return Decode(bytes,output,error);
    }
    static bool Seal(std::span<const EnhancedDrawItem> graph, EnhancedLatticeReplayInput& output, std::string& error)
    {
        try
        {
            if(graph.size()>4096) throw std::runtime_error("Lattice replay draw count exceeds budget");
            EnhancedLatticeReplayInput candidate;
            for(const auto& item:graph)
            {
                const auto& instance=item.materialGraphInstance;
                if(!item.modelMeshView.IsComplete() || !instance || !instance->generation || !instance->generation->generation
                    || instance->description.graphId!=instance->generation->assetId || !item.coverage.IsValid()
                    || (item.coverage.flags&EnhancedMaterialCoverage::Blended))
                    throw std::runtime_error("Lattice replay requires exact immutable opaque/masked material owner");
                Draw d; d.geometryIds=EnhancedDrawReplayInput::Identity(item.modelMeshView);
                d.program=ProgramDigest(*instance->generation); d.coverage=item.coverage; d.description=instance->description;
                // Archive effective exposed values, including authored defaults.
                // Private constants are covered by the exact program digest.
                d.description.parameters.clear();
                for(const auto& binding:instance->generation->cooked.product.layout.parameters)
                    if(binding.parameter.exposed)
                    {
                        const auto found=std::ranges::find(instance->description.parameters,binding.parameter.id,&material_graph::ParameterOverride::id);
                        d.description.parameters.push_back({binding.parameter.id,found==instance->description.parameters.end() ? binding.parameter.value : found->value});
                    }
                std::ranges::sort(d.description.parameters,{},&material_graph::ParameterOverride::id);
                std::ranges::sort(d.description.textures,{},&material_graph::TextureOverride::parameter);
                std::vector<uint8_t> uniforms; std::vector<LX::LXMaterialDiagnostic> diagnostics;
                if(!material_graph::PrepareUniforms(instance->generation->cooked.product.layout,d.description.parameters,uniforms,diagnostics)
                    || uniforms!=instance->uniforms) throw std::runtime_error("Lattice replay typed values differ from sealed uniforms");
                for(const auto& t:instance->textures)
                {
                    if(!t.owner) throw std::runtime_error("Lattice replay texture owner missing");
                    d.textures.push_back({t.slot,static_cast<uint32_t>(t.colorSpace),t.assetId,TextureDigest(*t.owner)});
                }
                std::ranges::sort(d.textures,{},&TextureIdentity::slot);
                candidate.draws.push_back(std::move(d));
            }
            const auto bytes=candidate.Encode();
            EnhancedLatticeReplayInput checked;
            if(!Decode(bytes,checked,error)) return false;
            output=std::move(checked); error.clear(); return true;
        }
        catch(const std::exception& e) { error=e.what(); return false; }
    }
    bool Apply(std::span<EnhancedDrawItem> graph, std::string& error) const
    {
        EnhancedLatticeReplayInput current;
        if(!Seal(graph,current,error)) return false;
        if(draws.size()!=graph.size()) { error="Lattice replay selected draw count mismatch"; return false; }
        std::vector<std::shared_ptr<const material_graph::Instance>> instances;
        std::map<uint64_t,size_t> sharedSlots;
        for(size_t i=0;i<draws.size();++i)
        {
            const auto& saved=draws[i]; const auto& live=current.draws[i]; const auto& source=graph[i].materialGraphInstance;
            if(saved.geometryIds!=live.geometryIds || saved.description.graphId!=live.description.graphId
                || saved.program!=live.program || saved.textures!=live.textures)
            { error="Lattice replay geometry/program/texture closure mismatch"; return false; }
            // Retain exact current texture owners. No DataSystem/authoring lookup.
            const auto loader=[&](const experiment::AssetId& id,LX::LXColorSpace colorSpace,std::string& why) -> std::shared_ptr<Texture> {
                for(const auto& t:source->textures) if(t.assetId==id && t.colorSpace==colorSpace) return t.owner;
                why="Lattice replay texture override escaped current closure"; return {};
            };
            std::shared_ptr<const material_graph::Instance> instance;
            if(!material_graph::BuildInstance(source->generation,saved.description,loader,instance,error)) return false;
            if(instance->textures.size()!=saved.textures.size()) { error="Lattice replay rebuilt texture count mismatch"; return false; }
            for(const auto& t:instance->textures)
                if(!t.owner || std::ranges::none_of(saved.textures,[&](const auto& s){return s.slot==t.slot && s.assetId==t.assetId && s.colorSpace==static_cast<uint32_t>(t.colorSpace) && s.content==TextureDigest(*t.owner);}))
                { error="Lattice replay rebuilt texture binding mismatch"; return false; }
            const auto [slot,inserted]=sharedSlots.emplace(graph[i].materialGraphSlot,i);
            if(!inserted)
            {
                const auto& other=draws[slot->second];
                EnhancedLatticeReplayInput a,b; a.draws.push_back(saved); b.draws.push_back(other);
                a.draws[0].geometryIds={}; b.draws[0].geometryIds={};
                if(a.Encode()!=b.Encode()) { error="Lattice replay shared material slot has conflicting values"; return false; }
                instance=instances[slot->second];
            }
            instances.push_back(std::move(instance));
        }
        for(size_t i=0;i<draws.size();++i) { graph[i].materialGraphInstance=instances[i]; graph[i].coverage=draws[i].coverage; }
        error.clear(); return true;
    }
};
