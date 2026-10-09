#pragma once

#include "EnhancedRenderGraph.h"
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Producer-thread history. Capture copies alter the measured graph, so every
// lookup, eviction and failure watermark stays inside its measurement domain.
class EnhancedGpuMeasurementHistory
{
public:
    struct Sample
    {
        std::shared_ptr<const std::string> graphSignature;
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
        return found == samples.end() ? nullptr : &found->second;
    }

    const Sample* FindFresh(RGMeasurementDomain domain, const GpuFrameToken& current,
        uint64_t cpuTicksPerSecond) const
    {
        const auto* sample = Find(domain, current.renderViewId);
        if (!current.IsValid() || !sample || !sample->graphSignature || cpuTicksPerSecond == 0 ||
            sample->token.submissionId >= current.submissionId ||
            sample->token.engineFrameId > current.engineFrameId ||
            current.engineFrameId - sample->token.engineFrameId > kMaxFrameAge ||
            sample->token.cpuSubmitTick > current.cpuSubmitTick ||
            current.cpuSubmitTick - sample->token.cpuSubmitTick > cpuTicksPerSecond / 2)
        {
            return nullptr;
        }
        return sample;
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
        auto& sample = samples[token.renderViewId];
        sample = {};
        sample.token = token;
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
        samples[viewId] = std::move(sample);
        return true;
    }

private:
    using DomainSamples = std::unordered_map<uint64_t, Sample>;
    static constexpr size_t kMaxViews = 8;
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
                    return left.second.token.cpuSubmitTick < right.second.token.cpuSubmitTick;
                });
            samples.erase(oldest);
        }
    }
};
