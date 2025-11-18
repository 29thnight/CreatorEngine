#include "RenderGraphBuilder.h"

#include <queue>    // [Step2] 토폴로지 정렬용
#include <cassert>

// =======================
// PassBuilder 구현
// =======================

RenderGraphBuilder::PassBuilder::PassBuilder(RenderGraphBuilder& owner, std::size_t passIndex)
    : m_owner(owner)
    , m_passIndex(passIndex)
{
}

void RenderGraphBuilder::PassBuilder::ReadTexture(RGTextureHandle handle)
{
    if (!handle.IsValid()) return;
    if (m_passIndex >= m_owner.m_passes.size()) return;

    RenderGraphBuilder::PassNode& pass = m_owner.m_passes[m_passIndex];
    RenderGraphBuilder::TextureReadBinding binding{};
    binding.handle = handle;
    pass.reads.push_back(binding);
}

void RenderGraphBuilder::PassBuilder::WriteTexture(RGTextureHandle handle, RGTextureAccess access)
{
    if (!handle.IsValid()) return;
    if (m_passIndex >= m_owner.m_passes.size()) return;

    RenderGraphBuilder::PassNode& pass = m_owner.m_passes[m_passIndex];
    RenderGraphBuilder::TextureWriteBinding binding{};
    binding.handle = handle;
    binding.access = access;
    pass.writes.push_back(binding);
}

void RenderGraphBuilder::PassBuilder::SetExecuteCallback(const ExecuteCallback& callback)
{
    if (m_passIndex >= m_owner.m_passes.size()) return;
    m_owner.m_passes[m_passIndex].execute = callback;
}

// =======================
// RenderGraphBuilder 구현
// =======================

RenderGraphBuilder::RenderGraphBuilder() = default;
RenderGraphBuilder::~RenderGraphBuilder() = default;

void RenderGraphBuilder::Reset()
{
    m_textures.clear();
    m_passes.clear();
    m_executeOrder.clear();
}

RGTextureHandle RenderGraphBuilder::AddTexture(const RGTextureDesc& desc)
{
    RGTextureHandle handle{};
    handle.index = static_cast<uint16_t>(m_textures.size());

    TextureResource res{};
    res.desc = desc;
    res.imported = false;
    res.externalTexture = nullptr;

    m_textures.push_back(res);
    return handle;
}

RGTextureHandle RenderGraphBuilder::ImportExternalTexture(Texture* texture, const RGTextureDesc& desc)
{
    RGTextureHandle handle{};
    handle.index = static_cast<uint16_t>(m_textures.size());

    TextureResource res{};
    res.desc = desc;
    res.externalTexture = texture;
    res.imported = true;

    m_textures.push_back(res);
    return handle;
}

RGPassHandle RenderGraphBuilder::AddPass(const RGPassDesc& desc,
    const std::function<void(PassBuilder&)>& setup)
{
    RGPassHandle handle{};
    handle.index = static_cast<uint16_t>(m_passes.size());

    PassNode node{};
    node.desc = desc;

    m_passes.push_back(node);

    // 방금 추가된 패스에 대해 PassBuilder를 만들어 setup 람다 호출
    PassBuilder builder(*this, m_passes.size() - 1);
    if (setup)
    {
        setup(builder);
    }

    return handle;
}

// [Step2] 패스 의존성 분석 + 토폴로지 정렬
void RenderGraphBuilder::Compile()
{
    m_executeOrder.clear();

    const std::size_t passCount = m_passes.size();
    if (passCount == 0)
        return;

    // ----- 1) 텍스처별 "마지막 writer" 찾기 -----
    // 간단 버전: 하나의 텍스처에 여러 패스가 write하면
    //           마지막 write 패스가 이후 read 패스들의 의존성이 된다.
    //           (동일 텍스처에 여러 번 write 하는 복잡한 패턴은 나중에 고도화)
    std::vector<int> lastWriterForTexture(m_textures.size(), -1);

    for (std::size_t passIdx = 0; passIdx < passCount; ++passIdx)
    {
        const PassNode& pass = m_passes[passIdx];
        for (const auto& write : pass.writes)
        {
            if (!write.handle.IsValid()) continue;
            const std::size_t texIndex = write.handle.index;
            if (texIndex < lastWriterForTexture.size())
            {
                lastWriterForTexture[texIndex] = static_cast<int>(passIdx);
            }
        }
    }

    // ----- 2) 패스 그래프(Adjacency) 구성 -----
    std::vector<std::vector<std::size_t>> adj(passCount);
    std::vector<int> indegree(passCount, 0);

    auto AddEdge = [&](std::size_t from, std::size_t to)
        {
            if (from == to) return;
            adj[from].push_back(to);
            ++indegree[to];
        };

    for (std::size_t passIdx = 0; passIdx < passCount; ++passIdx)
    {
        const PassNode& pass = m_passes[passIdx];

        // 2-1) Read 의존성: 이 패스가 읽는 텍스처의 "마지막 writer"가 있으면 그 writer -> 이 패스
        for (const auto& read : pass.reads)
        {
            if (!read.handle.IsValid()) continue;
            const std::size_t texIndex = read.handle.index;
            if (texIndex >= lastWriterForTexture.size()) continue;

            int writerIdx = lastWriterForTexture[texIndex];
            if (writerIdx >= 0 && static_cast<std::size_t>(writerIdx) != passIdx)
            {
                AddEdge(static_cast<std::size_t>(writerIdx), passIdx);
            }
        }

        // 2-2) Write-After-Write 의존성: 같은 텍스처를 쓰는 패스들 간 순서 보장
        for (const auto& write : pass.writes)
        {
            if (!write.handle.IsValid()) continue;
            const std::size_t texIndex = write.handle.index;

            // 이전 writer들을 선형 탐색해서 의존성 추가 (단순 버전)
            for (std::size_t otherPassIdx = 0; otherPassIdx < passIdx; ++otherPassIdx)
            {
                const PassNode& otherPass = m_passes[otherPassIdx];
                for (const auto& otherWrite : otherPass.writes)
                {
                    if (!otherWrite.handle.IsValid()) continue;
                    if (otherWrite.handle.index == texIndex)
                    {
                        AddEdge(otherPassIdx, passIdx);
                    }
                }
            }
        }
    }

    // ----- 3) Kahn 알고리즘으로 토폴로지 정렬 -----
    std::queue<std::size_t> q;

    for (std::size_t i = 0; i < passCount; ++i)
    {
        if (indegree[i] == 0)
        {
            q.push(i);
        }
    }

    while (!q.empty())
    {
        const std::size_t u = q.front();
        q.pop();
        m_executeOrder.push_back(u);

        for (std::size_t v : adj[u])
        {
            if (--indegree[v] == 0)
            {
                q.push(v);
            }
        }
    }

    // 사이클이 있거나, 의존성 분석이 불완전해서 모든 노드가 정렬되지 않은 경우,
    // 남아있는 노드는 "등록 순서" 기준으로 뒷부분에 그냥 붙인다.
    if (m_executeOrder.size() != passCount)
    {
        // 간단한 fallback: indegree > 0 인 애들 중 아직 안 들어간 것들 추가
        for (std::size_t i = 0; i < passCount; ++i)
        {
            bool alreadyAdded = false;
            for (std::size_t idx : m_executeOrder)
            {
                if (idx == i)
                {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded)
            {
                m_executeOrder.push_back(i);
            }
        }
    }

    // 안전하게 방어: 크기가 정확히 passCount인지 assert (디버그용)
    assert(m_executeOrder.size() == passCount && "RenderGraphBuilder::Compile - execute order size mismatch");
}

void RenderGraphBuilder::Execute()
{
    if (m_passes.empty())
        return;

    // Compile()이 호출되지 않았다면, 기존처럼 등록 순서대로 실행
    if (m_executeOrder.empty())
    {
        for (PassNode& pass : m_passes)
        {
            if (pass.execute)
            {
                pass.execute();
            }
        }
        return;
    }

    // [Step2] 계산된 실행 순서대로 실행
    for (std::size_t passIdx : m_executeOrder)
    {
        if (passIdx >= m_passes.size()) continue;

        PassNode& pass = m_passes[passIdx];
        if (pass.execute)
        {
            pass.execute();
        }
    }
}
