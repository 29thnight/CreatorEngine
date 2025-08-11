#pragma once
#include "Core.Definition.h"

class GPUQueryCollector {
public:
    explicit GPUQueryCollector(ID3D11DeviceContext* ctx) : m_ctx(ctx), m_run(true) {
        m_thread = std::thread([this] { ThreadMain(); });
    }
    ~GPUQueryCollector() {
        { std::unique_lock lk(m_mtx); m_run = false; }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }

    struct Job {
        ComPtr<ID3D11Query> disjoint, start, end;
        std::promise<std::optional<double>> prom;
        std::wstring name;
    };

    std::future<std::optional<double>> Enqueue(ComPtr<ID3D11Query> dj,
        ComPtr<ID3D11Query> st,
        ComPtr<ID3D11Query> ed,
        std::wstring name)
    {
        Job j;
        j.disjoint = std::move(dj);
        j.start = std::move(st);
        j.end = std::move(ed);
        j.name = std::move(name);
        auto fut = j.prom.get_future();
        {
            std::unique_lock lk(m_mtx);
            m_jobs.push_back(std::move(j));
        }
        m_cv.notify_one();
        return fut;
    }

private:
    void ThreadMain() {
        for (;;) {
            Job job;
            {
                std::unique_lock lk(m_mtx);
                m_cv.wait(lk, [&] { return !m_run || !m_jobs.empty(); });
                if (!m_run && m_jobs.empty()) return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }

            auto waitReady = [&](ID3D11Query* q) {
                while (S_FALSE == m_ctx->GetData(q, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                };

            // 쿼리 준비될 때까지 폴링 (DONOTFLUSH 중요)
            waitReady(job.disjoint.Get());
            waitReady(job.start.Get());
            waitReady(job.end.Get());

            // 읽기
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            UINT64 s = 0, e = 0;
            m_ctx->GetData(job.disjoint.Get(), &dj, sizeof dj, 0);
            m_ctx->GetData(job.start.Get(), &s, sizeof s, 0);
            m_ctx->GetData(job.end.Get(), &e, sizeof e, 0);

            if (dj.Disjoint) job.prom.set_value(std::nullopt);
            else job.prom.set_value((double(e - s) / double(dj.Frequency)) * 1000.0);
        }
    }

    ComPtr<ID3D11DeviceContext> m_ctx;
    std::thread m_thread;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    bool m_run;
};

