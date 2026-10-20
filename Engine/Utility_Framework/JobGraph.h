#pragma once
#include "StandardLibrary.h"

// 제출 전에 고정하는 위상 순서다. 선행 ID는 현재 ID보다 작고 중복이 없어야 한다.
struct job_graph_node
{
    std::vector<uint32_t> dependencies;
};

struct job_graph_node_timing
{
    uint64_t registered_ns{};
    uint64_t ready_ns{};
    // 실제 큐 삽입 시각이 아니라 enki 게시 호출의 시작/종료다.
    // 큐가 가득 차 호출 스레드에서 실행되면 start_ns가 publish_end_ns보다 먼저 올 수 있다.
    uint64_t publish_begin_ns{};
    uint64_t publish_end_ns{};
    uint64_t start_ns{};
    uint64_t body_end_ns{};
    uint64_t complete_ns{};
    uint32_t publish_thread{};
    uint32_t execute_thread{};
    // enki 인덱스와 별개인 OS TID다. Windows 외에는 0(미제공)이다.
    uint32_t publish_os_thread{};
    uint32_t execute_os_thread{};
    bool dispatcher_route{};
    bool skipped{};
};

// 하나의 제출이 독점해서 기록하며, 최종 handle의 wait가 반환/예외를 전달한 뒤에만 읽는다.
// nullptr이면 노드별 저장소와 시계 조회를 모두 생략한다.
struct job_graph_diagnostics
{
    std::vector<job_graph_node_timing> nodes;
    uint64_t submitted_ns{};
    uint64_t dispatcher_begin_ns{};
    uint64_t completed_ns{};
    uint32_t dispatcher_handoffs{};
    uint32_t native_task_count{};
    bool native_storage_reused{};
};

uint64_t job_timestamp_ns() noexcept;
