#include "../VulkanSelfTest.h"
#include "Render/Graph/EnhancedMaterialSealHash.h"
#include "Render/Graph/EnhancedDrawSealLedger.h"
#include "Render/Scene/AuthoredMaterialDigest.h"
#include "Texture.h"

#include <cstdio>
#include <string>

// PBR-W8 — 세대 원자 밀봉의 값 신원과 장부를 잰다.
//
// 이 검사는 GPU를 켜지 않는다. W8이 닫는 결함의 본체가 픽셀이 아니라 **신원**
// 이기 때문이다. 같은 legacy Material을 공유하는 두 렌더러가 서로 다른 인스턴스
// override를 가질 때, 중복 제거가 주소로 이루어지면 뒤의 draw가 앞의 스냅샷을
// 받는다 — 화면에서는 "가끔 재질이 틀리다"로만 보이고 픽셀 검사로는 원인을
// 가리킬 수 없다. 그래서 값 digest와 장부를 직접 잰다.
//
// 픽셀 축은 기존 render.pbr.* 가, 실장면 축은 capture manifest의 sealLedger가
// 각각 맡는다. 여기서 그것을 흉내 내지 않는다.
namespace
{
    struct SealCaseLog
    {
        std::string& log;
        int passed{ 0 };
        int failed{ 0 };

        void Check(bool condition, const char* name)
        {
            if (condition) { ++passed; return; }
            ++failed;
            log += "  FAIL ";
            log += name;
            log += "\n";
        }
    };

    experiment::Material MakeAuthored(const char* name)
    {
        experiment::Material material;
        material.name = name;
        material.blendMode = experiment::MaterialBlendMode::Opaque;
        experiment::MaterialProperty base;
        base.name = "baseColor";
        base.value = math::vector4{ 1.f, 1.f, 1.f, 1.f };
        material.properties.push_back(base);
        experiment::MaterialProperty rough;
        rough.name = "roughness";
        rough.value = 0.5f;
        material.properties.push_back(rough);
        material.keywords.push_back("kUseNormalMap");
        return material;
    }

    EnhancedMaterialDrawSnapshot MakeSnapshot()
    {
        EnhancedMaterialDrawSnapshot snapshot;
        snapshot.shaderMetaHandle = ShaderMetaHandle{ 3u, 7u };
        snapshot.permutationKey = RHIShaderPermutationKey{ 0x1122334455667788ull, 0x99aabbccddeeff00ull };
        snapshot.bindingLayout.constantBufferName = "MaterialConstants";
        snapshot.bindingLayout.constantBufferRegister = 2u;
        snapshot.bindingLayout.constantBufferSpace = 0u;
        snapshot.bindingLayout.constantBufferByteSize = 48u;
        snapshot.keywordSelections = { 0u, 1u };
        snapshot.propertyBytes.assign(48u, std::uint8_t{ 0 });
        snapshot.propertyBytes[0] = 0x40;
        snapshot.coverage.flags = EnhancedMaterialCoverage::Enabled;
        snapshot.coverage.cutoff = 0.5f;
        snapshot.coverage.baseAlpha = 1.f;
        snapshot.useNormalMap = 1u;

        EnhancedMaterialTextureBinding binding;
        binding.propertyName = "baseColorMap";
        binding.registerIndex = 16u;
        binding.registerSpace = 0u;
        binding.coordinates.set = 0u;
        snapshot.textureBindings.push_back(binding);
        return snapshot;
    }

    // ① 값 digest — 같은 값은 같고, 한 자리라도 다르면 다르다.
    bool CheckDigestIdentity(SealCaseLog& cases)
    {
        const EnhancedMaterialDrawSnapshot base = MakeSnapshot();
        cases.Check(EnhancedMaterialSeal::ComputeHash(base)
            == EnhancedMaterialSeal::ComputeHash(MakeSnapshot()),
            "같은 값의 digest는 같아야 한다");
        cases.Check(0ull != EnhancedMaterialSeal::ComputeHash(base),
            "digest 0은 '없음'의 자리라 값으로 나오면 안 된다");

        const auto differs = [&](auto mutate, const char* name)
        {
            EnhancedMaterialDrawSnapshot other = MakeSnapshot();
            mutate(other);
            cases.Check(EnhancedMaterialSeal::ComputeHash(base)
                != EnhancedMaterialSeal::ComputeHash(other), name);
        };
        differs([](auto& s) { s.shaderMetaHandle.generation = 8u; },
            "ShaderMeta generation이 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.permutationKey.lo ^= 1ull; },
            "permutation이 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.propertyBytes[4] = 0x7f; },
            "property bytes가 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.keywordSelections[1] = 2u; },
            "keyword 선택이 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.coverage.cutoff = 0.25f; },
            "coverage cutoff가 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.useNormalMap = 0u; },
            "useNormalMap이 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.textureBindings[0].registerIndex = 17u; },
            "texture register가 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.textureBindings[0].coordinates.set = 1u; },
            "UV set이 다르면 digest가 달라야 한다");
        differs([](auto& s) { s.textureBindings.clear(); },
            "texture 개수가 다르면 digest가 달라야 한다");

        // -0.0과 +0.0은 같은 값이다. 비트로만 접으면 여기서 갈린다.
        EnhancedMaterialDrawSnapshot negativeZero = MakeSnapshot();
        negativeZero.coverage.baseAlpha = 0.f;
        EnhancedMaterialDrawSnapshot positiveZero = MakeSnapshot();
        positiveZero.coverage.baseAlpha = -0.f;
        cases.Check(EnhancedMaterialSeal::ComputeHash(negativeZero)
            == EnhancedMaterialSeal::ComputeHash(positiveZero),
            "-0.0과 +0.0은 같은 값이므로 digest도 같아야 한다");
        return 0 == cases.failed;
    }

    // ② 저작 digest — 인스턴스 override가 다르면 반드시 갈려야 한다.
    //    이것이 갈리지 않으면 sealing의 중복 제거가 override를 삼킨다.
    bool CheckAuthoredDigest(SealCaseLog& cases)
    {
        const experiment::Material base = MakeAuthored("Shared");
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(base)
            == EnhancedAuthoredMaterialDigest::Compute(MakeAuthored("Shared")),
            "같은 저작 값의 digest는 같아야 한다");

        experiment::Material overridden = MakeAuthored("Shared");
        overridden.properties[1].value = 0.25f;   // roughness override
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(base)
            != EnhancedAuthoredMaterialDigest::Compute(overridden),
            "property override가 다르면 저작 digest가 달라야 한다");

        experiment::Material keyword = MakeAuthored("Shared");
        keyword.keywords.push_back("ALPHA_TEST");
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(base)
            != EnhancedAuthoredMaterialDigest::Compute(keyword),
            "keyword override가 다르면 저작 digest가 달라야 한다");

        experiment::Material blend = MakeAuthored("Shared");
        blend.blendMode = experiment::MaterialBlendMode::Masked;
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(base)
            != EnhancedAuthoredMaterialDigest::Compute(blend),
            "blend mode가 다르면 저작 digest가 달라야 한다");

        // 타입이 다른데 값 표현이 같은 경우. variant 인덱스를 접지 않으면
        // 0u와 0.f가 같은 digest를 받는다.
        experiment::Material asUint = MakeAuthored("Shared");
        asUint.properties[1].value = std::uint32_t{ 0 };
        experiment::Material asFloat = MakeAuthored("Shared");
        asFloat.properties[1].value = 0.f;
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(asUint)
            != EnhancedAuthoredMaterialDigest::Compute(asFloat),
            "값 타입이 다르면 저작 digest가 달라야 한다");

        // texture override는 assetId·색공간·좌표까지 신원이다.
        experiment::Material texture = MakeAuthored("Shared");
        experiment::MaterialProperty reference;
        reference.name = "baseColorMap";
        experiment::TextureReference value;
        value.logicalName = "albedo";
        value.colorSpace = experiment::TextureColorSpace::Srgb;
        reference.value = value;
        texture.properties.push_back(reference);
        experiment::Material linear = texture;
        std::get<experiment::TextureReference>(linear.properties.back().value)
            .colorSpace = experiment::TextureColorSpace::Linear;
        cases.Check(EnhancedAuthoredMaterialDigest::Compute(texture)
            != EnhancedAuthoredMaterialDigest::Compute(linear),
            "texture 색공간이 다르면 저작 digest가 달라야 한다");
        return 0 == cases.failed;
    }

    // ③ 장부 — 프레임 신원과 배치 혼합 판정.
    bool CheckLedger(SealCaseLog& cases)
    {
        EnhancedMaterialDrawSnapshot snapshot = MakeSnapshot();
        EnhancedMaterialSeal::Stamp(snapshot, 0x1234ull, 2ull, 9ull, 5ull, 100ull);

        EnhancedDrawSealLedger ledger;
        ledger.Begin(100ull, 5ull);
        cases.Check(ledger.Accept(snapshot.seal,
            EnhancedMaterialSeal::ComputeHash(snapshot)),
            "이번 프레임의 밀봉은 통과해야 한다");
        cases.Check(1u == ledger.counters.stamped && 0u == ledger.counters.skipped,
            "통과한 draw는 stamped로만 세어야 한다");

        // 지난 프레임의 밀봉이 섞여 들어온 경우.
        ledger.Begin(101ull, 5ull);
        cases.Check(!ledger.Accept(snapshot.seal,
            EnhancedMaterialSeal::ComputeHash(snapshot)),
            "지난 프레임의 밀봉은 거부해야 한다");
        cases.Check(1u == ledger.counters.staleFrame && 1u == ledger.counters.skipped,
            "거부는 staleFrame으로 세어야 한다");

        // epoch만 다른 경우도 같은 프레임이 아니다.
        ledger.Begin(100ull, 6ull);
        cases.Check(!ledger.Accept(snapshot.seal,
            EnhancedMaterialSeal::ComputeHash(snapshot)),
            "sceneEpoch가 다르면 같은 프레임이 아니다");

        // 밀봉 뒤 값이 바뀐 경우 — 불변식 위반이다.
        ledger.Begin(100ull, 5ull);
        EnhancedMaterialDrawSnapshot mutated = snapshot;
        mutated.propertyBytes[0] = 0x7f;
        cases.Check(!ledger.Accept(mutated.seal,
            EnhancedMaterialSeal::ComputeHash(mutated)),
            "밀봉 뒤 값이 바뀌면 거부해야 한다");
        cases.Check(1u == ledger.counters.valueMismatch,
            "값 변경은 valueMismatch로 세어야 한다");

        // 도장이 없는 격리 fixture는 staleness 축을 재지 않는다.
        ledger.Begin(100ull, 5ull);
        EnhancedMaterialDrawSnapshot unstamped = MakeSnapshot();
        cases.Check(ledger.Accept(unstamped.seal, EnhancedMaterialSeal::ComputeHash(unstamped)),
            "도장 없는 fixture snapshot은 통과해야 한다");
        cases.Check(1u == ledger.counters.unstamped && 0u == ledger.counters.stamped,
            "도장 없는 draw는 unstamped로 세어야 한다");

        // 같은 값이 서로 다른 PSO로 그려지면 혼합이다.
        ledger.Begin(100ull, 5ull);
        EnhancedDrawSealLedger::Binding first{};
        first.pipelineId = 11u;
        first.textureDigest = 0xAAAAull;
        first.samplerIdentity = 0xBBBBull;
        cases.Check(ledger.Observe(snapshot.seal.sealHash, first),
            "처음 본 배치는 기록되어야 한다");
        cases.Check(ledger.Observe(snapshot.seal.sealHash, first),
            "같은 배치를 다시 봐도 충돌이 아니다");
        EnhancedDrawSealLedger::Binding otherPipeline = first;
        otherPipeline.pipelineId = 12u;
        cases.Check(!ledger.Observe(snapshot.seal.sealHash, otherPipeline),
            "같은 seal이 다른 PSO로 그려지면 거부해야 한다");
        cases.Check(1u == ledger.counters.pipelineConflict,
            "PSO 혼합은 pipelineConflict로 세어야 한다");

        EnhancedDrawSealLedger::Binding otherTextures = first;
        otherTextures.textureDigest = 0xCCCCull;
        cases.Check(!ledger.Observe(snapshot.seal.sealHash, otherTextures),
            "같은 seal이 다른 texture 묶음으로 그려지면 거부해야 한다");
        EnhancedDrawSealLedger::Binding otherSampler = first;
        otherSampler.samplerIdentity = 0xDDDDull;
        cases.Check(!ledger.Observe(snapshot.seal.sealHash, otherSampler),
            "같은 seal이 다른 sampler로 그려지면 거부해야 한다");
        cases.Check(2u == ledger.counters.bindingConflict,
            "배치 혼합은 bindingConflict로 세어야 한다");

        // 기록 단계 누락은 이유별로 세고 위반 합계에 들어간다.
        ledger.NoteDrop(EnhancedDrawDropReason::Pipeline);
        ledger.NoteDrop(EnhancedDrawDropReason::Bindings, 2u);
        cases.Check(3u == ledger.recordDrops.Total(),
            "기록 단계 누락은 이유별로 합산되어야 한다");
        cases.Check(1u == ledger.recordDrops.Get(EnhancedDrawDropReason::Pipeline)
            && 2u == ledger.recordDrops.Get(EnhancedDrawDropReason::Bindings),
            "누락 이유가 서로 섞이면 안 된다");
        cases.Check(ledger.Violations() >= 6u,
            "위반 합계는 신원 위반과 기록 누락을 함께 세어야 한다");

        // Begin은 지난 프레임의 수를 지운다 — 지우지 않으면 다음 프레임이
        // 지난 프레임의 위반으로 붉어진다.
        ledger.Begin(102ull, 5ull);
        cases.Check(0u == ledger.Violations() && ledger.bindings.empty(),
            "Begin은 장부를 비워야 한다");
        return 0 == cases.failed;
    }

    // ④ sampler 신원 — 지금은 패스가 고정 sampler 하나를 걸지만, 무엇을
    //    걸었는지는 값으로 남아야 한다.
    bool CheckSamplerIdentity(SealCaseLog& cases)
    {
        const RHISamplerDesc wrap = RHISampler::Linear(RHIAddressMode::Wrap);
        const RHISamplerDesc clamp = RHISampler::Linear(RHIAddressMode::Clamp);
        cases.Check(EnhancedMaterialSeal::ComputeSamplerIdentity(wrap)
            == EnhancedMaterialSeal::ComputeSamplerIdentity(
                RHISampler::Linear(RHIAddressMode::Wrap)),
            "같은 sampler 설정은 같은 신원이어야 한다");
        cases.Check(EnhancedMaterialSeal::ComputeSamplerIdentity(wrap)
            != EnhancedMaterialSeal::ComputeSamplerIdentity(clamp),
            "address mode가 다르면 sampler 신원이 달라야 한다");

        RHISamplerDesc point = wrap;
        point.minMag = RHIFilterMode::Point;
        cases.Check(EnhancedMaterialSeal::ComputeSamplerIdentity(wrap)
            != EnhancedMaterialSeal::ComputeSamplerIdentity(point),
            "filter가 다르면 sampler 신원이 달라야 한다");

        RHISamplerDesc mip0 = wrap;
        mip0.maxLod = 0.f;
        cases.Check(EnhancedMaterialSeal::ComputeSamplerIdentity(wrap)
            != EnhancedMaterialSeal::ComputeSamplerIdentity(mip0),
            "maxLod가 다르면 sampler 신원이 달라야 한다 (mip 0만 읽는 함정)");
        return 0 == cases.failed;
    }
}

bool RunPbrSealTest(std::string& outLog)
{
    SealCaseLog cases{ outLog };
    CheckDigestIdentity(cases);
    CheckAuthoredDigest(cases);
    CheckLedger(cases);
    CheckSamplerIdentity(cases);

    char line[256]{};
    std::snprintf(line, sizeof(line),
        "PBR seal: %d cases, failed %d\n", cases.passed + cases.failed, cases.failed);
    outLog += line;
    return 0 == cases.failed;
}
