#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "EnhancedRenderGraph.h"
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

// Producer-thread history. Capture copies alter the measured graph, so every
// lookup, eviction and failure watermark stays inside its measurement domain.
class EnhancedGpuMeasurementHistory
{
public:
    struct Sample
    {
        own::shared_owner<const std::string> graphSignature;
        std::vector<std::optional<uint64_t>> nanoseconds;
        GpuFrameToken token;
    };

    void Clear()
    {
        for (auto& domain : m_domains)
        {
            domain.clear();
        }
    }

    const Sample* Find(RGMeasurementDomain domain, uint64_t viewId) const
    {
        const auto& samples = Samples(domain);
        const auto found = samples.find(viewId);
        return found == samples.end() ? nullptr : &found->second.latest;
    }

    const Sample* FindFresh(RGMeasurementDomain domain, const GpuFrameToken& current,
        uint64_t cpuTicksPerSecond, const own::shared_owner<const std::string>& signature = {}) const
    {
        const auto fresh = [&](const Sample* sample)
        {
            return current.IsValid() && sample && sample->graphSignature && cpuTicksPerSecond != 0 &&
            (!signature || sample->graphSignature.operator->() == signature.operator->() || *sample->graphSignature == *signature) &&
            !(
            sample->token.submissionId >= current.submissionId ||
            sample->token.engineFrameId > current.engineFrameId ||
            current.engineFrameId - sample->token.engineFrameId > kMaxFrameAge ||
            sample->token.cpuSubmitTick > current.cpuSubmitTick ||
            current.cpuSubmitTick - sample->token.cpuSubmitTick > cpuTicksPerSecond / 2);
        };
        const auto* latest = Find(domain, current.renderViewId);
        if (fresh(latest)) return latest;
        // Alternating history-buffer roles are distinct valid declarations.
        // Match exact signatures; never merge costs across graph variants.
        if (signature && latest && latest->graphSignature)
        {
            for (const auto& sample : Samples(domain).at(current.renderViewId).variants)
                if (fresh(&sample)) return &sample;
        }
        return nullptr;
    }

    void Discard(RGMeasurementDomain domain, const GpuFrameToken& token)
    {
        const auto* previous = Find(domain, token.renderViewId);
        if (!token.IsValid() || (previous && previous->token.submissionId > token.submissionId))
        {
            return;
        }
        // Retain a failed submission's watermark: collecting an older successful
        // token later must not restore a sample invalidated by this failure.
        auto& samples = Samples(domain);
        MakeRoom(samples, token.renderViewId);
        auto& entry = samples[token.renderViewId];
        entry = {};
        entry.latest.token = token;
    }

    bool Store(RGMeasurementDomain domain, Sample sample)
    {
        const auto* previous = Find(domain, sample.token.renderViewId);
        if (!sample.token.IsValid() || !sample.graphSignature ||
            (previous && previous->token.submissionId >= sample.token.submissionId))
        {
            return false;
        }
        auto& samples = Samples(domain);
        MakeRoom(samples, sample.token.renderViewId);
        const auto viewId = sample.token.renderViewId;
        auto& entry = samples[viewId];
        if (entry.latest.graphSignature)
        {
            const auto equal = [](const auto& a, const auto& b)
            { return a.operator->() == b.operator->() || (a && b && *a == *b); };
            std::erase_if(entry.variants, [&](const Sample& old)
            { return equal(old.graphSignature, sample.graphSignature); });
            if (!equal(entry.latest.graphSignature, sample.graphSignature))
                entry.variants.push_back(std::move(entry.latest));
            if (entry.variants.size() >= kMaxSignaturesPerView)
                entry.variants.erase(entry.variants.begin());
        }
        entry.latest = std::move(sample);
        return true;
    }

private:
    struct Entry { Sample latest; std::vector<Sample> variants; };
    using DomainSamples = std::unordered_map<uint64_t, Entry>;
    static constexpr size_t kMaxViews = 8;
    static constexpr size_t kMaxSignaturesPerView = 4;
    static constexpr uint64_t kMaxFrameAge = 8;
    std::array<DomainSamples, 2> m_domains;

    DomainSamples& Samples(RGMeasurementDomain domain)
    {
        return m_domains[domain == RGMeasurementDomain::Capture ? 1 : 0];
    }

    const DomainSamples& Samples(RGMeasurementDomain domain) const
    {
        return m_domains[domain == RGMeasurementDomain::Capture ? 1 : 0];
    }

    static void MakeRoom(DomainSamples& samples, uint64_t viewId)
    {
        if (samples.find(viewId) == samples.end() && samples.size() >= kMaxViews)
        {
            const auto oldest = std::min_element(samples.begin(), samples.end(),
                [](const auto& left, const auto& right)
                {
                    return left.second.latest.token.cpuSubmitTick < right.second.latest.token.cpuSubmitTick;
                });
            samples.erase(oldest);
        }
    }
};
