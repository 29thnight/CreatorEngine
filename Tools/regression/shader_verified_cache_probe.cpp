#include "RHIShaderCompiler.h"
#include "RHIShaderVerifiedCache.h"
#include "PathFinder.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>

using Verified = RHIShaderCompiler::VerifiedShader;
static void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);std::cout<<"CHECK "<<name<<'\n';}
static void Write(const std::filesystem::path& p,std::string_view text){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<text;if(!f)throw std::runtime_error("fixture write");}
static std::string Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static bool Same(const Verified& a,const Verified& b){return a.reflection==b.reflection && a.bytecode.Size()==b.bytecode.Size() && !std::memcmp(a.bytecode.Data(),b.bytecode.Data(),a.bytecode.Size()) && a.dependencyIdentity==b.dependencyIdentity;}
static Verified Verify(const RHIShaderCompileOptions& options,RHIShaderBinary backend=RHIShaderBinary::Dxil,const RHIShaderPermutation& permutation={})
{
 Verified shader;std::string error;
 if(!RHIShaderCompiler::VerifyFile("CacheProbe.slang","CSMain","cs_6_0",backend,permutation,shader,error,options))throw std::runtime_error(error);
 return shader;
}
int main(int argc,char** argv)try
{
 if(argc!=4)throw std::runtime_error("probe <repo> <work> <mode>");
 const auto repo=std::filesystem::absolute(argv[1]),work=std::filesystem::absolute(argv[2]);const std::string mode=argv[3];
 const auto root=work/"Assets/Shaders/DefaultPassShader",includes=work/"Includes";
 std::filesystem::create_directories(root);std::filesystem::create_directories(includes);
 auto* paths=InternalPath::GetInstance();paths->BaseProjectPath=repo/"Dynamic_CPP";paths->ShaderSourcePath=work/"Assets/Shaders";paths->AssetAuthoringEnabled=true;
 RHIShaderCompileOptions options;options.includeDirectories.push_back(includes);
 const std::string source=R"(#include "CacheLeaf.hlsli"
#ifndef PROBE_SCALE
#define PROBE_SCALE 1
#endif
cbuffer Params : register(b1,space0) { float4 tint; float3x3 transform; uint flags[2]; };
Texture2D<float4> Input : register(t3,space0);
SamplerState Sampling : register(s4,space0);
RWStructuredBuffer<float4> Output : register(u5,space0);
[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID) {
 Output[id.x]=Input.SampleLevel(Sampling,float2(0.2,0.3),0)*tint*PROBE_SCALE+float4(transform[0]*LeafValue(),flags[0]);
})";
 if(mode=="populate")
 {
  Write(root/"CacheProbe.slang",source);Write(includes/"CacheLeaf.hlsli","#include \"Nested.hlsli\"\nfloat LeafValue(){return NestedValue();}\n");Write(includes/"Nested.hlsli","float NestedValue(){return 0.25;}\n");
  RHIShaderBlob bytecode;std::string error;Check(RHIShaderCompiler::CompileFile("CacheProbe.slang","CSMain","cs_6_0",bytecode,error,options),"bytecode-only compile");
  RHIShaderReflection initialReflection;Check(RHIShaderCompiler::ReflectFile("CacheProbe.slang","CSMain","cs_6_0",RHIShaderBinary::Dxil,initialReflection,error,options),"bytecode-only then fresh reflection");
  auto dx=Verify(options);Check(dx.reflection==initialReflection,"fresh and verified reflection agree");Check(RHIShaderCompiler::GetStats().compiles==2,"bytecode-only cannot satisfy verification");
  auto again=Verify(options);Check(Same(dx,again)&&RHIShaderCompiler::GetStats().memoryHits==1,"verified memory hit preserves pair and identity");
  RHIShaderReflection reflection;Check(RHIShaderCompiler::ReflectFile("CacheProbe.slang","CSMain","cs_6_0",RHIShaderBinary::Dxil,reflection,error,options)&&reflection==dx.reflection,"verified then reflection");
  RHIShaderBlob reverse;Check(RHIShaderCompiler::CompileFile("CacheProbe.slang","CSMain","cs_6_0",reverse,error,options)&&reverse.Size()==dx.bytecode.Size()&&!std::memcmp(reverse.Data(),dx.bytecode.Data(),reverse.Size()),"verified then bytecode");
  auto spirv=Verify(options,RHIShaderBinary::SpirV);if(!AreShaderReflectionsEquivalent(dx.reflection,spirv.reflection,error)) { std::cerr << "PARITY_DETAIL " << error << "\n"; for(const auto& r:dx.reflection.resources) std::cerr << "DX " << r.name << " bytes=" << r.byteSize << " index=" << r.registerIndex << " space=" << r.registerSpace << "\n";for(const auto& r:spirv.reflection.resources) std::cerr << "SP " << r.name << " bytes=" << r.byteSize << " index=" << r.registerIndex << " space=" << r.registerSpace << "\n";} Check(error.empty(),"real DXIL SPIR-V layout parity");
  for(const auto& [name,shader]:{std::pair{"dxil",dx},std::pair{"spirv",spirv}}){std::vector<std::uint8_t> data;Check(rhi_shader_verified_cache::Encode(shader.bytecode,shader.reflection,data),"expected encode");std::ofstream f(work/(std::string(name)+".expected"),std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());Write(work/(std::string(name)+".identity"),shader.dependencyIdentity);}
 }
 else if(mode=="restart")
 {
  for(const auto& [name,backend]:{std::pair{"dxil",RHIShaderBinary::Dxil},std::pair{"spirv",RHIShaderBinary::SpirV}}){auto shader=Verify(options,backend);std::vector<std::uint8_t> data;Check(rhi_shader_verified_cache::Encode(shader.bytecode,shader.reflection,data),"hit encode");const auto expected=Read(work/(std::string(name)+".expected"));Check(expected.size()==data.size()&&!std::memcmp(expected.data(),data.data(),data.size())&&Read(work/(std::string(name)+".identity"))==shader.dependencyIdentity,"restart identical bytecode reflection identity");}
  const auto stats=RHIShaderCompiler::GetStats();Check(stats.compiles==0&&stats.diskHits==2,"two persistent backend hits with zero compiles");
 }
 else if(mode=="invalidation")
 {
  auto initial=Verify(options);Write(includes/"Nested.hlsli","float NestedValue(){return 0.75;}\n");auto nested=Verify(options);Check(nested.dependencyIdentity!=initial.dependencyIdentity,"transitive include content invalidates");
  Write(root/"CacheLeaf.hlsli","float LeafValue(){return 0.9;}\n");auto shadow=Verify(options);Check(shadow.dependencyIdentity!=nested.dependencyIdentity,"new higher priority include invalidates");
  Verified profile;std::string profileError;Check(RHIShaderCompiler::VerifyFile("CacheProbe.slang","CSMain","cs_6_1",RHIShaderBinary::Dxil,{},profile,profileError,options)&&profile.dependencyIdentity!=shadow.dependencyIdentity,"target profile invalidates");
  auto strict=options;strict.strictMath=true;Check(Verify(strict).dependencyIdentity!=shadow.dependencyIdentity,"effective strict math invalidates");
  RHIShaderPermutation permutation;std::string error;Check(permutation.Set("PROBE_SCALE","2",error),"permutation set");Check(Verify(options,RHIShaderBinary::Dxil,permutation).dependencyIdentity!=shadow.dependencyIdentity,"permutation invalidates");
  auto previous=shadow;Write(root/"CacheProbe.slang","this is not shader syntax");Check(!RHIShaderCompiler::VerifyFile("CacheProbe.slang","CSMain","cs_6_0",RHIShaderBinary::Dxil,{},previous,error,options)&&Same(previous,shadow),"compile failure preserves previous verified pair");Write(root/"CacheProbe.slang",source);
 }
 else if(mode=="corruption")
 {
  auto expected=Verify(options);
  wchar_t local[32768]{};Check(GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768)!=0,"private cache root");const auto file=std::filesystem::path(local)/"CreatorEngine/ShaderCache/v1"/(expected.dependencyIdentity+".rsv");
  for(const std::string kind:{"missing","truncated","old-schema","checksum","trailing"})
  {
   if(kind=="missing")std::filesystem::remove(file);
   else{auto bytes=Read(file);if(kind=="truncated")bytes.resize(5);if(kind=="old-schema")bytes[4]=0;if(kind=="checksum")bytes.back()^=1;if(kind=="trailing")bytes.push_back(0);Write(file,bytes);}
   RHIShaderCompiler::ClearMemoryCache();RHIShaderCompiler::ResetStats();Check(Same(expected,Verify(options))&&RHIShaderCompiler::GetStats().compiles==1,"bad record fresh compile preserves pair");
   RHIShaderCompiler::ClearMemoryCache();RHIShaderCompiler::ResetStats();Check(Same(expected,Verify(options))&&RHIShaderCompiler::GetStats().diskHits==1&&RHIShaderCompiler::GetStats().compiles==0,"fallback repairs persistent record");
  }
  const auto blocked=work/"BlockedAppData";std::filesystem::create_directories(blocked);Write(blocked/"CreatorEngine","block cache directory creation");SetEnvironmentVariableW(L"LOCALAPPDATA",blocked.c_str());RHIShaderCompiler::ClearMemoryCache();Check(Same(expected,Verify(options)),"cache write failure does not fail compilation");Check(Same(expected,Verify(options)),"valid pair remains in memory after write failure");
 }
 else if(mode=="race")
 {
  std::atomic<bool> okay{true};std::vector<std::thread> threads;std::vector<Verified> results(6);
  for(int i=0;i<6;++i)threads.emplace_back([&,i]{try{results[i]=Verify(options);if(!results[i].bytecode.IsValid()||results[i].reflection.resources.empty())okay=false;}catch(...){okay=false;}});
  for(auto& thread:threads)thread.join();for(const auto& result:results)if(!Same(results[0],result))okay=false;Check(okay,"concurrent requests produce identical complete pairs");
 }
 else throw std::runtime_error("unknown mode");
 const auto stats=RHIShaderCompiler::GetStats();std::cout<<"SHADER_VERIFIED_CACHE_PROBE_OK mode="<<mode<<" memoryHits="<<stats.memoryHits<<" diskHits="<<stats.diskHits<<" compiles="<<stats.compiles<<" failures="<<stats.failures<<'\n';return 0;
}
catch(const std::exception& error){std::cerr<<"PROBE_FAILED "<<error.what()<<'\n';return 1;}
