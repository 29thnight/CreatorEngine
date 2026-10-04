#include "CookedEnvironment.h"
#include "../RHI/DX12/EnhancedIBLGenerator.h"
#include "../Experiment/Cooked/CookedAssetManifest.h"
#include "../../EngineDiagnostics/ProfileScope.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <vector>
#include <cmath>
#include <Windows.h>

namespace assets
{
namespace
{
constexpr char kEnvironmentMagic[8]{'C','E','I','B','L','0','0','5'};
constexpr char kEnvironmentSourceMagic[8]{'C','E','I','B','L','0','0','4'};
constexpr char kEnvironmentFloatMagic[8]{'C','E','I','B','L','0','0','3'};
constexpr char kEnvironmentHalfMagic[8]{'C','E','I','B','L','0','0','2'};
constexpr char kEnvironmentLegacyMagic[8]{'C','E','I','B','L','0','0','1'};
constexpr uint64_t kEnvironmentMaxBytes = 512ull * 1024 * 1024;
std::string EnvironmentHex(const Hash::Sha256Digest& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (uint8_t byte : value) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
bool EnvironmentError(std::string& error, std::string message)
{
    error = std::move(message); return false;
}
bool EnvironmentSizes(uint32_t cube, uint32_t brdf)
{
    return cube >= 32 && cube <= 1024 && (cube & (cube - 1)) == 0 && brdf >= 16 &&
           brdf <= 1024 && (brdf & (brdf - 1)) == 0;
}
std::array<TextureImage, 4> EnvironmentImages(uint32_t cube, uint32_t brdf, bool halfRadiance = false)
{
    const auto radiance = halfRadiance ? RHIFormat::RGBA16Float : EnhancedIBLGenerator::kRadianceFormat;
    return {TextureImage::Allocate(radiance, cube, cube, 6,
                EnhancedIBLGenerator::CubeMipCount(cube), true),
            TextureImage::Allocate(RHIFormat::RGBA16Float, std::min(cube,64u), std::min(cube,64u), 6, 1, true),
            TextureImage::Allocate(radiance, cube, cube, 6, EnhancedIBLGenerator::kPrefilterMips, true),
            TextureImage::Allocate(RHIFormat::RGBA16Float, brdf, brdf, 1, 1)};
}
std::array<TextureImage, 3> ImportanceImages(uint32_t size, uint32_t samples = 1024)
{
    return {TextureImage::Allocate(RHIFormat::RGBA32Float,size,6*size,1,1),
        TextureImage::Allocate(RHIFormat::RGBA32Float,1,6*size,1,1),
        TextureImage::Allocate(RHIFormat::RGBA32Float,samples,2,1,1)};
}
float EnvironmentHalf(uint16_t h)
{
    const float magnitude = (h & 0x7c00) == 0 ? std::ldexp(float(h & 1023),-24)
        : (h & 0x7c00) == 0x7c00 ? INFINITY
        : std::ldexp(float(1024+(h & 1023)),int((h >> 10) & 31)-25);
    return h & 0x8000 ? -magnitude : magnitude;
}
float* ImportancePixel(TextureImage& image, uint32_t x, uint32_t y)
{
    const auto& slice=*image.Find(0,0);
    return reinterpret_cast<float*>(image.MutablePixelsAt(slice)+y*slice.rowPitch+x*16);
}
const float* ImportancePixel(const TextureImage& image, uint32_t x, uint32_t y)
{
    const auto& slice=*image.Find(0,0);
    return reinterpret_cast<const float*>(slice.pixels+y*slice.rowPitch+x*16);
}
bool EnvironmentCollectShader(const std::filesystem::path& file, const std::filesystem::path& root,
    std::set<std::filesystem::path>& files, std::string& error)
{
    const auto normalized = file.lexically_normal();
    if (!files.insert(normalized).second) return true;
    std::ifstream input(normalized);
    if (!input) return EnvironmentError(error, "Environment shader missing: " + normalized.string());
    std::string line;
    while (std::getline(input, line))
    {
        const auto include = line.find("#include");
        if (include == std::string::npos) continue;
        const auto begin = line.find('"', include), end = begin == std::string::npos ? begin : line.find('"', begin+1);
        if (end == std::string::npos) continue;
        const auto name = line.substr(begin+1, end-begin-1);
        auto target = normalized.parent_path() / name;
        if (!std::filesystem::is_regular_file(target)) target = root / "Includes" / name;
        if (!EnvironmentCollectShader(target, root, files, error)) return false;
    }
    return true;
}
}

bool BuildEnvironmentImportance(CookedEnvironment& value, std::string& error)
{
    const auto& cube=value.images[0];
    if (!cube.IsValid() || (cube.Format()!=RHIFormat::RGBA16Float && cube.Format()!=RHIFormat::RGBA32Float) || !cube.IsCube() ||
        cube.ArraySize()!=6 || cube.Width()!=value.cubeSize || cube.Height()!=value.cubeSize)
        return EnvironmentError(error,"Importance proposal requires the cooked radiance cube");
    const uint32_t size=value.cubeSize, rows=6*size;
    value.importance=ImportanceImages(size);
    double total=0;
    for (uint32_t row=0;row<rows;++row)
    {
        const auto& slice=*cube.Find(0,row/size);
        double rowTotal=0;
        for (uint32_t x=0;x<size;++x)
        {
            const bool half = cube.Format()==RHIFormat::RGBA16Float;
            const auto* input = slice.pixels+(row%size)*slice.rowPitch+x*(half ? 8 : 16);
            float* cell=ImportancePixel(value.importance[0],x,row);
            for (unsigned channel=0;channel<3;++channel)
            {
                const auto radiance=half ? EnvironmentHalf(reinterpret_cast<const uint16_t*>(input)[channel])
                    : reinterpret_cast<const float*>(input)[channel];
                if (!std::isfinite(radiance)) return EnvironmentError(error,"Nonfinite environment radiance");
                cell[channel]=std::max(radiance,0.f);
            }
            const double u=(x+.5)*2/size-1, v=(row%size+.5)*2/size-1;
            rowTotal+=(.2126*cell[0]+.7152*cell[1]+.0722*cell[2])*std::pow(1+u*u+v*v,-1.5);
            cell[3]=float(rowTotal);
        }
        total+=rowTotal;
        ImportancePixel(value.importance[1],0,row)[0]=float(total);
    }
    // Cube UV area is not solid angle. Use the same Jacobian for cached
    // sample PDFs and shader lookup PDFs. CDF textures must not be filtered.
    const double storedTotal=ImportancePixel(value.importance[1],0,rows-1)[0];
    for (uint32_t i=0;i<1024;++i)
    {
        uint32_t bits=i;
        bits=(bits<<16)|(bits>>16); bits=((bits&0x55555555)<<1)|((bits&0xaaaaaaaa)>>1);
        bits=((bits&0x33333333)<<2)|((bits&0xcccccccc)>>2);
        bits=((bits&0x0f0f0f0f)<<4)|((bits&0xf0f0f0f0)>>4);
        bits=((bits&0x00ff00ff)<<8)|((bits&0xff00ff00)>>8);
        const double xi=(i+.5)/1024, yi=bits*2.3283064365386963e-10;
        float* sample=ImportancePixel(value.importance[2],i,0);
        if (storedTotal<=0)
        {
            const double z=1-2*xi, r=std::sqrt(std::max(1-z*z,0.));
            sample[0]=float(r*std::cos(2*3.141592653589793*yi));
            sample[1]=float(r*std::sin(2*3.141592653589793*yi));
            sample[2]=float(z); sample[3]=float(1/(4*3.141592653589793)); continue;
        }
        double target=xi*storedTotal;
        uint32_t low=0,high=rows-1;
        while(low<high) {auto mid=(low+high)/2; if(ImportancePixel(value.importance[1],0,mid)[0]<=target) low=mid+1; else high=mid;}
        const uint32_t row=low;
        const double before=row?ImportancePixel(value.importance[1],0,row-1)[0]:0;
        const double after=ImportancePixel(value.importance[1],0,row)[0];
        const double fy=std::clamp((target-before)/std::max(after-before,1e-20),1e-4,1.-1e-4);
        target=yi*ImportancePixel(value.importance[0],size-1,row)[3]; low=0;high=size-1;
        while(low<high) {auto mid=(low+high)/2; if(ImportancePixel(value.importance[0],mid,row)[3]<=target) low=mid+1; else high=mid;}
        const uint32_t x=low; const auto* cell=ImportancePixel(value.importance[0],x,row);
        const double cellBefore=x?ImportancePixel(value.importance[0],x-1,row)[3]:0;
        const double fx=std::clamp((target-cellBefore)/std::max(double(cell[3])-cellBefore,1e-20),1e-4,1.-1e-4);
        const double u=(x+fx)*2/size-1,v=(row%size+fy)*2/size-1;
        double d[3];
        switch(row/size) {
        case 0:d[0]=1;d[1]=-v;d[2]=-u;break;case 1:d[0]=-1;d[1]=-v;d[2]=u;break;
        case 2:d[0]=u;d[1]=1;d[2]=v;break;case 3:d[0]=u;d[1]=-1;d[2]=-v;break;
        case 4:d[0]=u;d[1]=-v;d[2]=1;break;default:d[0]=-u;d[1]=-v;d[2]=-1;break;}
        const double length=std::sqrt(1+u*u+v*v);
        for(unsigned c=0;c<3;++c) sample[c]=float(d[c]/length);
        const double cu=(x+.5)*2/size-1,cv=(row%size+.5)*2/size-1;
        const double weight=(.2126*cell[0]+.7152*cell[1]+.0722*cell[2])*std::pow(1+cu*cu+cv*cv,-1.5);
        sample[3]=float(weight/storedTotal*(.25*size*size)*std::pow(1+u*u+v*v,1.5));
    }
    error.clear(); return true;
}

bool EnvironmentSourceIdentity(const std::filesystem::path& source, Hash::Sha256Digest& result, std::string& error)
{
    ce::profile_scope profile{ ce::marker<"Environment.SourceIdentity">() };
    std::ifstream input(source, std::ios::binary);
    if (!input) return EnvironmentError(error, "Environment source missing: " + source.string());
    Hash::Sha256 hash;
    std::array<char, 65536> chunk;
    while (input) { input.read(chunk.data(), chunk.size()); hash.Update(chunk.data(), size_t(input.gcount())); }
    if (!input.eof()) return EnvironmentError(error, "Environment source read failed");
    result = hash.Finish(); error.clear(); return true;
}

bool EnvironmentRecipeIdentity(const std::filesystem::path& shaders, uint32_t cubeSize,
    uint32_t brdfSize, Hash::Sha256Digest& result, std::string& error)
{
    ce::profile_scope profile{ ce::marker<"Environment.RecipeIdentity">() };
    if (!EnvironmentSizes(cubeSize, brdfSize)) return EnvironmentError(error, "Invalid environment cook sizes");
    constexpr const char* stages[]{"IblFace.slang", "IblFullscreen.slang", "IblRectToCube.slang",
        "IblSourceCopy.slang", "IblCubeDownsample.slang", "IblIrradiance.slang", "IblPrefilter.slang", "IblBrdf.slang",
        "IblImportanceRows.slang", "IblImportanceMarginal.slang", "IblImportanceSamples.slang","IblSceneImportance.slang"};
    std::set<std::filesystem::path> files;
    for (const auto* stage : stages)
        if (!EnvironmentCollectShader(shaders / stage, shaders, files, error)) return false;
    Hash::Sha256 hash;
    constexpr char recipe[] = "ce.environment.v5;source-rgba32f-mip0;cube-prefilter-rgba32f;irradiance-brdf-rgba16f;linear-rec709;E-over-pi;D3D-cube;MIS1024;prefilter6;envMips7;sceneCDF-mip0-rgba32f-banks1024+4096";
    hash.Update(recipe, sizeof(recipe));
    hash.Update(&cubeSize, sizeof(cubeSize)); hash.Update(&brdfSize, sizeof(brdfSize));
    for (const auto& file : files)
    {
        const auto relative = file.lexically_relative(shaders).generic_string();
        Hash::Sha256Digest digest;
        if (!EnvironmentSourceIdentity(file, digest, error)) return false;
        hash.Update(relative.data(), relative.size()); hash.Update(digest.data(), digest.size());
    }
    result = hash.Finish(); error.clear(); return true;
}

std::string EnvironmentCacheName(const EnvironmentIdentity& identity)
{
    return EnvironmentHex(identity.source) + "-" + EnvironmentHex(identity.recipe) + ".ceibl";
}

bool ReadCookedEnvironment(const std::filesystem::path& file, CookedEnvironment& result,
    std::string& error, const EnvironmentIdentity* expected)
{
    ce::profile_scope profile{ ce::marker<"Environment.ReadCooked">() };
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return EnvironmentError(error, "Cooked environment missing: " + file.string());
    const auto size = input.tellg();
    if (size < 112 || uint64_t(size) > kEnvironmentMaxBytes) return EnvironmentError(error, "Invalid cooked environment size");
    input.seekg(0);
    std::array<char, 8> magic;
    Hash::Sha256Digest digest;
    input.read(magic.data(), magic.size()); input.read(reinterpret_cast<char*>(digest.data()), digest.size());
    std::vector<std::byte> body(size_t(size)-40);
    input.read(reinterpret_cast<char*>(body.data()), body.size());
    Hash::Sha256Digest actual;
    const bool legacy = std::memcmp(magic.data(), kEnvironmentLegacyMagic, 8) == 0;
    const bool halfRadiance = legacy || std::memcmp(magic.data(), kEnvironmentHalfMagic, 8) == 0;
    const bool denseProposal = std::memcmp(magic.data(), kEnvironmentMagic, 8) == 0;
    const bool hasSource = denseProposal || std::memcmp(magic.data(), kEnvironmentSourceMagic, 8) == 0;
    if (!input || (!halfRadiance && !hasSource && std::memcmp(magic.data(), kEnvironmentFloatMagic, 8) != 0) ||
        ![&] { ce::profile_scope shaProfile{ ce::marker<"Environment.Sha">() }; return experiment::cooked::ComputeSha256(body, actual, error); }() || actual != digest)
        return EnvironmentError(error, "Cooked environment signature/checksum differs");
    CookedEnvironment candidate;
    std::memcpy(&candidate.cubeSize, body.data(), 4); std::memcpy(&candidate.brdfSize, body.data()+4, 4);
    std::memcpy(candidate.identity.source.data(), body.data()+8, 32);
    std::memcpy(candidate.identity.recipe.data(), body.data()+40, 32);
    if (!EnvironmentSizes(candidate.cubeSize, candidate.brdfSize) ||
        (expected && candidate.identity != *expected))
        return EnvironmentError(error, "Cooked environment source/recipe/settings differ");
    candidate.images = EnvironmentImages(candidate.cubeSize, candidate.brdfSize, halfRadiance);
    if (!legacy) candidate.importance = ImportanceImages(candidate.cubeSize,
        denseProposal ? EnhancedIBLGenerator::kSceneImportanceSampleCount : 1024u);
    const size_t header = hasSource ? 80 : 72;
    if (body.size() < header) return EnvironmentError(error,"Cooked source header is incomplete");
    if (hasSource)
    {
        uint32_t width{}, height{};
        std::memcpy(&width,body.data()+72,4); std::memcpy(&height,body.data()+76,4);
        if (!width || !height || width > 16384 || height > 16384 ||
            uint64_t(width)*height*16 > kEnvironmentMaxBytes)
            return EnvironmentError(error,"Cooked source dimensions differ");
        candidate.source = TextureImage::Allocate(RHIFormat::RGBA32Float,width,height,1,1);
    }
    size_t required = header + candidate.source.TotalBytes();
    for (const auto& image : candidate.images) required += image.TotalBytes();
    if (!legacy) for (const auto& image : candidate.importance) required += image.TotalBytes();
    if (body.size() != required) return EnvironmentError(error, "Cooked environment payload layout differs");
    size_t offset = header;
    for (auto& image : candidate.images)
        for (uint32_t face=0; face<image.ArraySize(); ++face)
            for (uint32_t mip=0; mip<image.MipLevels(); ++mip)
            {
                const auto& slice = *image.Find(mip,face);
                std::memcpy(image.MutablePixelsAt(slice), body.data()+offset, slice.slicePitch);
                offset += slice.slicePitch;
            }
    if (legacy)
    {
        // Old authoring caches remain readable. Current recipes miss these
        // files and cook v5; the distributed default never rebuilds CDFs.
        if (!BuildEnvironmentImportance(candidate,error)) return false;
    }
    else for (auto& image : candidate.importance)
    {
        const auto& slice=*image.Find(0,0);
        std::memcpy(image.MutablePixelsAt(slice),body.data()+offset,slice.slicePitch);
        offset+=slice.slicePitch;
    }
    if (hasSource)
    {
        const auto& slice=*candidate.source.Find(0,0);
        std::memcpy(candidate.source.MutablePixelsAt(slice),body.data()+offset,slice.slicePitch);
    }
    candidate.importancePersisted=!legacy;
    result = std::move(candidate); error.clear(); return true;
}

bool WriteCookedEnvironment(const std::filesystem::path& file, const CookedEnvironment& value, std::string& error)
{
    if (!EnvironmentSizes(value.cubeSize, value.brdfSize)) return EnvironmentError(error, "Invalid environment cook settings");
    if (!value.source.IsValid() || value.source.Format()!=RHIFormat::RGBA32Float ||
        value.source.IsCube() || value.source.ArraySize()!=1 || value.source.MipLevels()!=1 ||
        value.source.Width()>16384 || value.source.Height()>16384)
        return EnvironmentError(error,"Environment source layout differs");
    std::vector<std::byte> body(80);
    size_t bytes = 80 + value.source.TotalBytes();
    for (const auto& image : value.images) bytes += image.TotalBytes();
    for (const auto& image : value.importance) bytes += image.TotalBytes();
    if (bytes > kEnvironmentMaxBytes - 40) return EnvironmentError(error,"Environment cook payload is too large");
    body.reserve(bytes);
    std::memcpy(body.data(), &value.cubeSize, 4); std::memcpy(body.data()+4, &value.brdfSize, 4);
    std::memcpy(body.data()+8, value.identity.source.data(),32); std::memcpy(body.data()+40, value.identity.recipe.data(),32);
    const uint32_t sourceWidth=value.source.Width(), sourceHeight=value.source.Height();
    std::memcpy(body.data()+72,&sourceWidth,4); std::memcpy(body.data()+76,&sourceHeight,4);
    for (size_t index=0; index<4; ++index)
    {
        const auto& image = value.images[index];
        const uint32_t size = index==1 ? std::min(value.cubeSize,64u) : index==3 ? value.brdfSize : value.cubeSize;
        const uint32_t mips = index==0 ? EnhancedIBLGenerator::CubeMipCount(size) : index==2 ? EnhancedIBLGenerator::kPrefilterMips : 1u;
        if (!image.IsValid() || image.Format()!=EnhancedIBLGenerator::CookedImageFormat(uint32_t(index)) || image.Width()!=size ||
            image.Height()!=size || image.ArraySize()!=(index<3 ? 6u : 1u) || image.MipLevels()!=mips ||
            image.IsCube()!=(index<3)) return EnvironmentError(error,"Environment cook image layout differs");
        for (uint32_t face=0; face<image.ArraySize(); ++face)
            for (uint32_t mip=0; mip<image.MipLevels(); ++mip)
            {
                const auto& slice = *image.Find(mip,face);
                body.insert(body.end(), slice.pixels, slice.pixels+slice.slicePitch);
            }
    }
    for (size_t index=0;index<3;++index)
    {
        const auto& image=value.importance[index];
        const uint32_t sampleCount=value.importance[2].Width();
        if (sampleCount!=1024 && sampleCount!=EnhancedIBLGenerator::kSceneImportanceSampleCount)
            return EnvironmentError(error,"Environment proposal bank layout differs");
        const uint32_t width=index==0?value.cubeSize:index==1?1:sampleCount;
        const uint32_t height=index==2?2:6*value.cubeSize;
        if (!image.IsValid() || image.Format()!=RHIFormat::RGBA32Float || image.Width()!=width ||
            image.Height()!=height || image.ArraySize()!=1 || image.MipLevels()!=1 || image.IsCube())
            return EnvironmentError(error,"Environment importance layout differs");
        const auto& slice=*image.Find(0,0);
        body.insert(body.end(),slice.pixels,slice.pixels+slice.slicePitch);
    }
    const auto& sourceSlice=*value.source.Find(0,0);
    body.insert(body.end(),sourceSlice.pixels,sourceSlice.pixels+sourceSlice.slicePitch);
    Hash::Sha256Digest digest;
    if (!experiment::cooked::ComputeSha256(body,digest,error)) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(),ec);
    if (ec) return EnvironmentError(error,"Cannot create environment cache directory: "+ec.message());
    auto temporary = file; temporary += ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetCurrentThreadId());
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    const char* magic=value.importance[2].Width()==1024 ? kEnvironmentSourceMagic : kEnvironmentMagic;
    output.write(magic,8); output.write(reinterpret_cast<const char*>(digest.data()),digest.size());
    output.write(reinterpret_cast<const char*>(body.data()),body.size()); output.close();
    if (!output || !MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary,ec);
        return EnvironmentError(error,"Cannot publish cooked environment atomically");
    }
    error.clear(); return true;
}
}
