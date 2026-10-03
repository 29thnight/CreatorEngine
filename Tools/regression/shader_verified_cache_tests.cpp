#include "RHIShaderVerifiedCache.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

static void Require(bool value, const char* name)
{
    if (!value) { std::cerr << "FAIL " << name << '\n'; std::exit(1); }
}
int main()
{
    const unsigned char code[]{0x44,0x58,0x42,0x43,1,2,3,4};
    RHIShaderBlob source;
    source.Assign(code,sizeof(code));
    RHIShaderReflection reflection;
    reflection.stage = RHIShaderStage::Compute;
    for (unsigned kind=0;kind<=static_cast<unsigned>(RHIShaderResourceKind::StorageByteAddressBuffer);++kind)
    {
        RHIShaderResourceReflection resource;
        resource.name="resource"+std::to_string(kind);
        resource.kind=static_cast<RHIShaderResourceKind>(kind);
        resource.registerIndex=kind+2;resource.registerSpace=3;resource.arrayElements=7;
        resource.byteSize=512;
        if (!kind)
            for (unsigned scalar=0;scalar<=static_cast<unsigned>(RHIShaderScalarKind::Float32);++scalar)
                resource.fields.push_back({"field"+std::to_string(scalar),
                    {static_cast<RHIShaderScalarKind>(scalar),3,4,2},scalar*96,96});
        reflection.resources.push_back(std::move(resource));
    }
    std::vector<std::uint8_t> payload;
    Require(rhi_shader_verified_cache::Encode(source,reflection,payload),"encode");
    RHIShaderBlob decoded;
    RHIShaderReflection layout;
    Require(rhi_shader_verified_cache::Decode(payload,reflection.stage,decoded,layout),"decode");
    Require(layout==reflection && decoded.Size()==source.Size() &&
        !std::memcmp(decoded.Data(),source.Data(),source.Size()),"all value fields round trip");
    for (std::size_t n=0;n<payload.size();++n)
    {
        RHIShaderBlob sentinel=source;RHIShaderReflection unchanged=reflection;
        Require(!rhi_shader_verified_cache::Decode(std::span(payload).first(n),reflection.stage,sentinel,unchanged),"truncation rejected");
        Require(unchanged==reflection && sentinel.Size()==source.Size(),"failed decode preserves outputs");
    }
    auto trailing=payload;trailing.push_back(0);
    Require(!rhi_shader_verified_cache::Decode(trailing,reflection.stage,decoded,layout),"trailing bytes");
    Require(!rhi_shader_verified_cache::Decode(payload,RHIShaderStage::Pixel,decoded,layout),"stage mismatch");
    auto oversized=payload;for(unsigned i=0;i<8;++i)oversized[i]=255;
    Require(!rhi_shader_verified_cache::Decode(oversized,reflection.stage,decoded,layout),"oversized blob");
    auto invalid=payload;invalid[8+sizeof(code)]=255;
    Require(!rhi_shader_verified_cache::Decode(invalid,reflection.stage,decoded,layout),"invalid stage");
    std::cout << "SHADER_VERIFIED_CACHE_CODEC_OK all-fields truncation bounds transactional\n";
}
