#pragma once

#include "IRenderPipelineCache.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// render owner가 unique request record로 장기 보관하는 그래픽 파이프라인 요청.
///
/// RHIGraphicsPipelineDesc 자체는 bytecode, input element, semantic 문자열을 모두
/// 빌려 쓴다. 따라서 ShaderMeta generation을 넘겨 다시 PSO를 만들 소유자가 desc만
/// 복사하면 source blob이 교체되는 순간 dangling pointer가 된다. 이 타입은 그 세
/// 저장소를 함께 소유하고 desc의 포인터를 매 move 뒤 다시 묶는다.
///
/// Replace는 새 pipeline을 먼저 준비한다. 성공한 뒤에만 옛 handle 하나를 retire하므로
/// compile 실패가 현재 draw 경로를 끊지 않는다. 같은 desc라 같은 handle이 돌아오면
/// 무효화하지 않고 소유 데이터만 새 generation의 사본으로 교체한다.
///
/// ★ 같은 cache handle을 다른 request가 공유한다면 기본 Replace가 그 holder도 stale로 만든다.
///   따라서 보통은 render owner가 desc/key별 record 하나만 둔다. 여러 논리 key가 동일 handle을
///   공유해야 하는 owner는 전체 holder를 한 경계에서 감사하고 `invalidatePrevious=false`로 교체한
///   뒤 마지막 holder가 사라질 때만 cache invalidation을 직접 수행해야 한다.
class RHIGraphicsPipelineRequest
{
public:
    RHIGraphicsPipelineRequest() = default;
    RHIGraphicsPipelineRequest(const RHIGraphicsPipelineRequest&) = delete;
    RHIGraphicsPipelineRequest& operator=(const RHIGraphicsPipelineRequest&) = delete;

    RHIGraphicsPipelineRequest(RHIGraphicsPipelineRequest&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    RHIGraphicsPipelineRequest& operator=(RHIGraphicsPipelineRequest&& other) noexcept
    {
        if (this != &other) MoveFrom(std::move(other));
        return *this;
    }

    bool Create(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& source,
        std::string& outError)
    {
        if (m_handle.IsValid())
        {
            outError = "이미 생성된 그래픽 파이프라인 요청이다";
            return false;
        }

        RHIGraphicsPipelineRequest candidate;
        if (!candidate.CopyFrom(source, outError)) return false;

        candidate.m_handle = cache.GetOrCreate(candidate.m_desc, outError);
        if (!candidate.m_handle.IsValid()) return false;

        *this = std::move(candidate);
        return true;
    }

    bool Replace(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& source,
        RHICompletionPoint retireAfter, std::string& outError,
        bool invalidatePrevious = true)
    {
        if (!m_handle.IsValid()) return Create(cache, source, outError);

        RHIGraphicsPipelineRequest candidate;
        if (!candidate.CopyFrom(source, outError)) return false;

        candidate.m_handle = cache.GetOrCreate(candidate.m_desc, outError);
        if (!candidate.m_handle.IsValid()) return false;

        const RHIPipelineHandle oldHandle = m_handle;
        if (candidate.m_handle != oldHandle && invalidatePrevious)
        {
            // false는 옛 handle이 global invalidation 등으로 이미 stale인 경우다.
            // 새 handle은 유효하므로 그 경우에도 요청을 복구한다.
            cache.InvalidatePipeline(oldHandle, retireAfter);
        }

        *this = std::move(candidate);
        return true;
    }

    bool IsValid() const { return m_handle.IsValid(); }
    RHIPipelineHandle GetHandle() const { return m_handle; }
    const RHIGraphicsPipelineDesc& GetDesc() const { return m_desc; }

private:
    bool CopyFrom(const RHIGraphicsPipelineDesc& source, std::string& outError)
    {
        if ((0 != source.vsSize && nullptr == source.vsBytecode) ||
            (0 != source.psSize && nullptr == source.psBytecode))
        {
            outError = "셰이더 바이트코드 크기와 포인터가 일치하지 않는다";
            return false;
        }
        if (0 != source.inputElementCount && nullptr == source.inputElements)
        {
            outError = "입력 레이아웃 개수와 포인터가 일치하지 않는다";
            return false;
        }

        m_desc = source;
        if (0 != source.vsSize)
        {
            const auto* begin = static_cast<const std::uint8_t*>(source.vsBytecode);
            m_vs.assign(begin, begin + source.vsSize);
        }
        if (0 != source.psSize)
        {
            const auto* begin = static_cast<const std::uint8_t*>(source.psBytecode);
            m_ps.assign(begin, begin + source.psSize);
        }

        m_inputElements.reserve(source.inputElementCount);
        m_semantics.reserve(source.inputElementCount);
        for (std::uint32_t i = 0; i < source.inputElementCount; ++i)
        {
            if (nullptr == source.inputElements[i].semantic)
            {
                outError = "입력 레이아웃 semantic이 비어 있다";
                return false;
            }
            m_inputElements.push_back(source.inputElements[i]);
            m_semantics.emplace_back(source.inputElements[i].semantic);
        }

        RebindPointers();
        return true;
    }

    void MoveFrom(RHIGraphicsPipelineRequest&& other) noexcept
    {
        m_desc = other.m_desc;
        m_vs = std::move(other.m_vs);
        m_ps = std::move(other.m_ps);
        m_inputElements = std::move(other.m_inputElements);
        m_semantics = std::move(other.m_semantics);
        m_handle = other.m_handle;
        RebindPointers();

        other.m_desc = {};
        other.m_handle = {};
    }

    void RebindPointers()
    {
        m_desc.vsBytecode = m_vs.empty() ? nullptr : m_vs.data();
        m_desc.vsSize = m_vs.size();
        m_desc.psBytecode = m_ps.empty() ? nullptr : m_ps.data();
        m_desc.psSize = m_ps.size();

        for (std::size_t i = 0; i < m_inputElements.size(); ++i)
            m_inputElements[i].semantic = m_semantics[i].c_str();
        m_desc.inputElements = m_inputElements.empty() ? nullptr : m_inputElements.data();
        m_desc.inputElementCount = static_cast<std::uint32_t>(m_inputElements.size());
    }

    RHIGraphicsPipelineDesc m_desc{};
    std::vector<std::uint8_t> m_vs;
    std::vector<std::uint8_t> m_ps;
    std::vector<RHIInputElement> m_inputElements;
    std::vector<std::string> m_semantics;
    RHIPipelineHandle m_handle{};
};
