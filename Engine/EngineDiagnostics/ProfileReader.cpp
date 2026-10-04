#include "ProfileReader.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>

// reader는 UI가, 실행 중인 작업은 실행기가 소유한다. 작업에는 공유 상태와
// 불변 입력만 넘기고 reader나 ImGui 객체를 참조하지 않는다.
namespace ce::detail::profile_reader_impl
{
    enum class request_kind
    {
        range,
        window,
        selection
    };

    struct preparation_request
    {
        request_kind kind_ = request_kind::window;
        capture_session_ptr capture_;
        capture_recording_ptr recording_;
        std::uint32_t first_ = 0;
        std::uint32_t last_ = 0;
        std::uint64_t first_ordinal_ = 0;
        std::uint32_t count_ = 0;
        std::uint64_t id_ = 0;
        std::uint64_t epoch_ = 0;
    };

    struct prepared_selection
    {
        capture_session_ptr capture_;
        frame_aggregate aggregate_;
    };

    struct preparation_state
    {
        std::mutex mutex_;
        bool canceled_ = false;
        bool running_ = false;
        bool range_failed_ = false;
        bool window_failed_ = false;
        bool selection_failed_ = false;
        std::uint64_t next_id_ = 0;
        std::uint64_t epoch_ = 0;
        std::optional<preparation_request> range_request_;
        std::optional<preparation_request> window_request_;
        std::optional<preparation_request> selection_request_;
        bool range_pending_ = false;
        bool window_pending_ = false;
        bool selection_pending_ = false;
        std::uint64_t range_done_ = 0;
        std::uint64_t window_done_ = 0;
        std::uint64_t selection_done_ = 0;
        capture_session_ptr range_ready_;
        std::uint64_t range_ready_id_ = 0;
        std::uint64_t range_ready_ordinal_ = 0;
        prepared_capture_window_ptr window_ready_;
        std::shared_ptr<const prepared_selection> selection_ready_;
    };

    bool current_request(const preparation_state& state, const preparation_request& request)
    {
        const auto& current = request.kind_ == request_kind::range ? state.range_request_
            : request.kind_ == request_kind::window ? state.window_request_ : state.selection_request_;
        if (state.canceled_ || state.epoch_ != request.epoch_ || !current)
        {
            return false;
        }
        // 라이브 갱신보다 준비가 오래 걸려도 첫 결과를 영원히 버리지 않는다.
        // 같은 출처 세대의 창은 먼저 완성된 쌍을 표시하고 최신 요청을 이어 간다.
        // Open/Clear가 세대를 바꾸면 과거 출처의 결과는 공개하지 않는다.
        return request.kind_ == request_kind::window || current->id_ == request.id_;
    }

    bool is_current(const std::shared_ptr<preparation_state>& state, const preparation_request& request)
    {
        std::lock_guard lock(state->mutex_);
        return current_request(*state, request);
    }

    prepared_capture_window_ptr prepare_window(const std::shared_ptr<preparation_state>& state,
                                              const preparation_request& request)
    {
        auto result = std::make_shared<prepared_capture_window>();
        result->capture_ = request.capture_;
        result->first_frame_ = request.first_;
        result->last_frame_ = request.last_;
        // 실측 PT 지연의 대부분은 인덱스 이전의 복사·정렬이었다. 두 단계
        // 모두 같은 작업에서 끝낸 뒤 하나의 불변 결과로 공개한다.
        result->aggregate_ = aggregate_frames(*request.capture_, request.first_, request.last_,
                                              aggregate_scope::spans_only);
        if (!is_current(state, request))
        {
            return {};
        }
        const auto summaries = result->aggregate_.threads();
        result->threads_.assign(summaries.begin(), summaries.end());
        const auto spans = result->aggregate_.spans();
        for (const thread_summary& thread : result->threads_)
        {
            if (!is_current(state, request))
            {
                return {};
            }
            std::map<std::uint16_t, std::vector<timeline_interval>> depths;
            for (std::uint32_t index = thread.span_begin; index < thread.span_end && index < spans.size(); ++index)
            {
                const profile_event& span = spans[index];
                if (has_flag(span.flags, event_flags::instant))
                {
                    continue;
                }
                auto& intervals = depths[span.depth];
                const profile_tick previousEnd = intervals.empty() ? 0 : intervals.back().prefix_end_;
                intervals.push_back({index, (std::max)(previousEnd, span.tick_end)});
            }
            auto& rows = result->lanes_[thread.thread_slot];
            rows.reserve(depths.size());
            for (auto& [depth, intervals] : depths)
            {
                rows.push_back({depth, std::move(intervals)});
            }
        }
        for (const thread_info& info : request.capture_->threads())
        {
            if (info.name != "[RenderThread]" || info.slot > UINT16_MAX)
            {
                continue;
            }
            const auto found = std::find_if(result->threads_.begin(), result->threads_.end(),
                [&](const thread_summary& thread) { return thread.thread_slot == info.slot; });
            if (found == result->threads_.end())
            {
                thread_summary empty{};
                empty.thread_slot = static_cast<std::uint16_t>(info.slot);
                result->threads_.push_back(empty);
            }
        }
        const auto infoFor = [&request](std::uint16_t slot) -> const thread_info*
        {
            for (const thread_info& info : request.capture_->threads())
            {
                if (info.slot == slot)
                {
                    return &info;
                }
            }
            return nullptr;
        };
        std::sort(result->threads_.begin(), result->threads_.end(),
            [&](const thread_summary& left, const thread_summary& right)
            {
                const thread_info* a = infoFor(left.thread_slot);
                const thread_info* b = infoFor(right.thread_slot);
                if (a && b)
                {
                    return track_precedes(*a, *b);
                }
                if (a || b)
                {
                    return a != nullptr;
                }
                return left.thread_slot < right.thread_slot;
            });
        return result;
    }

    void run_preparation(const std::shared_ptr<preparation_state>& state)
    {
        bool preferSelection = false;
        for (;;)
        {
            preparation_request request;
            {
                std::lock_guard lock(state->mutex_);
                if (state->canceled_ || (!state->range_pending_ && !state->window_pending_ &&
                                          !state->selection_pending_))
                {
                    state->running_ = false;
                    return;
                }
                if (state->range_pending_)
                {
                    request = *state->range_request_;
                    state->range_pending_ = false;
                }
                else if (state->window_pending_ && (!state->selection_pending_ || !preferSelection))
                {
                    request = *state->window_request_;
                    state->window_pending_ = false;
                    preferSelection = true;
                }
                else
                {
                    request = *state->selection_request_;
                    state->selection_pending_ = false;
                    preferSelection = false;
                }
            }
            capture_session_ptr range;
            prepared_capture_window_ptr window;
            std::shared_ptr<const prepared_selection> selection;
            bool failed = false;
            try
            {
                if (request.kind_ == request_kind::range)
                {
                    const auto loaded = request.recording_->load_range(
                        request.first_ordinal_, request.count_, 128u * 1024u * 1024u);
                    if (loaded)
                    {
                        range = *loaded;
                    }
                    else
                    {
                        failed = true;
                    }
                }
                else if (request.kind_ == request_kind::window)
                {
                    window = prepare_window(state, request);
                }
                else
                {
                    auto result = std::make_shared<prepared_selection>();
                    result->capture_ = request.capture_;
                    result->aggregate_ = aggregate_frames(*request.capture_, request.first_, request.last_);
                    selection = std::move(result);
                }
            }
            catch (...)
            {
                failed = true;
            }
            // 교체된 큰 자료는 잠금을 푼 뒤 파괴한다. UI와 공유하는 임계
            // 구간에는 고정 크기 메타데이터와 포인터 교환만 남긴다.
            {
                std::lock_guard lock(state->mutex_);
                if (!current_request(*state, request))
                {
                    continue;
                }
                if (request.kind_ == request_kind::range)
                {
                    state->range_failed_ = failed;
                    state->range_done_ = request.id_;
                    if (!failed && range)
                    {
                        state->range_ready_.swap(range);
                        state->range_ready_id_ = request.id_;
                        state->range_ready_ordinal_ = request.first_ordinal_;
                    }
                }
                else if (request.kind_ == request_kind::window)
                {
                    state->window_failed_ = failed;
                    state->window_done_ = request.id_;
                    if (!failed && window)
                    {
                        state->window_ready_.swap(window);
                    }
                }
                else
                {
                    state->selection_failed_ = failed;
                    state->selection_done_ = request.id_;
                    if (!failed && selection)
                    {
                        state->selection_ready_.swap(selection);
                    }
                }
            }
        }
    }

    void fail_admission(const std::shared_ptr<preparation_state>& state)
    {
        std::lock_guard lock(state->mutex_);
        state->running_ = false;
        state->range_failed_ = state->range_failed_ || state->range_pending_;
        state->window_failed_ = state->window_failed_ || state->window_pending_;
        state->selection_failed_ = state->selection_failed_ || state->selection_pending_;
        state->range_pending_ = state->window_pending_ = state->selection_pending_ = false;
        state->range_done_ = state->range_request_ ? state->range_request_->id_ : 0;
        state->window_done_ = state->window_request_ ? state->window_request_->id_ : 0;
        state->selection_done_ = state->selection_request_ ? state->selection_request_->id_ : 0;
    }

    struct preparation_admission
    {
        std::shared_ptr<preparation_state> state_;
        std::atomic_bool started_{false};

        explicit preparation_admission(std::shared_ptr<preparation_state> state) : state_(std::move(state)) {}

        ~preparation_admission()
        {
            // 스케줄러는 작업 배치 실패를 예외 대신 실패한 완료 핸들로 돌려줄
            // 수도 있다. 본문 호출 없이 콜백이 폐기되면 busy를 반드시 해제한다.
            if (!started_.load(std::memory_order_acquire))
            {
                fail_admission(state_);
            }
        }
    };

    void start_preparation(const std::shared_ptr<preparation_state>& state, const preparation_dispatch& dispatch)
    {
        bool start = false;
        {
            std::lock_guard lock(state->mutex_);
            if (!state->canceled_ && !state->running_ &&
                (state->range_pending_ || state->window_pending_ || state->selection_pending_))
            {
                state->running_ = true;
                start = true;
            }
        }
        if (!start)
        {
            return;
        }
        try
        {
            const auto admission = std::make_shared<preparation_admission>(state);
            dispatch([admission]
            {
                admission->started_.store(true, std::memory_order_release);
                run_preparation(admission->state_);
            });
        }
        catch (...)
        {
            fail_admission(state);
        }
    }
}

namespace ce
{
    capture_reader::capture_reader(preparation_dispatch dispatch) : m_dispatch(std::move(dispatch))
    {
        if (m_dispatch)
        {
            m_preparation = std::make_shared<detail::profile_reader_impl::preparation_state>();
        }
    }

    capture_reader::~capture_reader()
    {
        if (m_preparation)
        {
            std::lock_guard lock(m_preparation->mutex_);
            m_preparation->canceled_ = true;
        }
        // UI는 future/join/wait를 하지 않는다. 실행기는 종료 시 승인한 작업을
        // 배출하고, 그때까지 공유 소유권이 입력 자료의 수명을 보장한다.
    }

    void capture_reader::cancel_preparation()
    {
        using namespace detail::profile_reader_impl;
        std::optional<preparation_request> rangeRequest;
        std::optional<preparation_request> windowRequest;
        std::optional<preparation_request> selectionRequest;
        capture_session_ptr range;
        prepared_capture_window_ptr window;
        std::shared_ptr<const prepared_selection> selection;
        if (m_preparation)
        {
            // 같은 작업 상태를 유지한다. Open/Clear마다 새 상태를 만들면
            // 취소 중인 작업과 새 작업이 동시에 실행될 수 있다.
            std::lock_guard lock(m_preparation->mutex_);
            ++m_preparation->epoch_;
            rangeRequest.swap(m_preparation->range_request_);
            windowRequest.swap(m_preparation->window_request_);
            selectionRequest.swap(m_preparation->selection_request_);
            m_preparation->range_pending_ = false;
            m_preparation->window_pending_ = false;
            m_preparation->selection_pending_ = false;
            range.swap(m_preparation->range_ready_);
            window.swap(m_preparation->window_ready_);
            selection.swap(m_preparation->selection_ready_);
            m_preparation->range_failed_ = false;
            m_preparation->window_failed_ = false;
            m_preparation->selection_failed_ = false;
        }
        m_preparedWindow.reset();
        m_preparedSelection.reset();
        m_appliedRangeRequest = 0;
    }

    void capture_reader::request_preparation(bool window) const
    {
        if (!m_preparation || !m_capture || m_capture->frame_count() == 0)
        {
            return;
        }
        using namespace detail::profile_reader_impl;
        const std::uint32_t first = window ? graph_first() : m_selectedFirst;
        const std::uint32_t last = window ? graph_last() : m_selectedLast;
        std::optional<preparation_request> retired;
        {
            std::lock_guard lock(m_preparation->mutex_);
            auto& request = window ? m_preparation->window_request_ : m_preparation->selection_request_;
            if (!request || request->capture_ != m_capture || request->first_ != first || request->last_ != last)
            {
                preparation_request next;
                next.kind_ = window ? request_kind::window : request_kind::selection;
                next.capture_ = m_capture;
                next.first_ = first;
                next.last_ = last;
                next.id_ = ++m_preparation->next_id_;
                next.epoch_ = m_preparation->epoch_;
                retired.swap(request);
                request = std::move(next);
                (window ? m_preparation->window_pending_ : m_preparation->selection_pending_) = true;
                (window ? m_preparation->window_failed_ : m_preparation->selection_failed_) = false;
            }
        }
        start_preparation(m_preparation, m_dispatch);
    }

    prepared_capture_window_ptr capture_reader::prepared_window() const
    {
        if (!m_preparation)
        {
            return {};
        }
        request_preparation(true);
        prepared_capture_window_ptr ready;
        {
            std::unique_lock lock(m_preparation->mutex_, std::try_to_lock);
            if (lock.owns_lock())
            {
                ready = m_preparation->window_ready_;
            }
        }
        if (ready)
        {
            m_preparedWindow = std::move(ready);
        }
        return m_preparedWindow;
    }

    void capture_reader::open_file(capture_recording_ptr recording)
    {
        reset();
        m_liveFollow = false;
        m_recording = std::move(recording);
        if (m_recording && m_recording->frame_count() > 0)
        {
            const std::uint64_t count = m_recording->frame_count();
            request_recording_range(count > 600 ? count - 600 : 0);
        }
    }

    void capture_reader::request_recording_range(std::uint64_t first_ordinal, std::uint32_t count)
    {
        if (!m_preparation || !m_recording || m_recording->frame_count() == 0)
        {
            return;
        }
        using namespace detail::profile_reader_impl;
        m_liveFollow = false;
        first_ordinal = (std::min)(first_ordinal, m_recording->frame_count() - 1);
        count = (std::clamp)(count, 1u, 600u);
        count = static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(count),
                                                    m_recording->frame_count() - first_ordinal));
        std::optional<preparation_request> retired;
        {
            std::lock_guard lock(m_preparation->mutex_);
            const auto& previous = m_preparation->range_request_;
            if (previous && previous->recording_ == m_recording && previous->first_ordinal_ == first_ordinal &&
                previous->count_ == count && !m_preparation->range_failed_)
            {
                return;
            }
            preparation_request request;
            request.kind_ = request_kind::range;
            request.recording_ = m_recording;
            request.first_ordinal_ = first_ordinal;
            request.count_ = count;
            request.id_ = ++m_preparation->next_id_;
            request.epoch_ = m_preparation->epoch_;
            retired.swap(m_preparation->range_request_);
            m_preparation->range_request_ = std::move(request);
            m_preparation->range_pending_ = true;
            m_preparation->range_failed_ = false;
        }
        start_preparation(m_preparation, m_dispatch);
    }

    void capture_reader::poll_preparation()
    {
        if (!m_preparation)
        {
            return;
        }
        capture_session_ptr range;
        std::uint64_t ordinal = 0;
        std::uint64_t request = 0;
        {
            std::unique_lock lock(m_preparation->mutex_, std::try_to_lock);
            if (lock.owns_lock() && m_preparation->range_ready_ && m_preparation->range_request_ &&
                m_preparation->range_ready_id_ == m_preparation->range_request_->id_ &&
                m_preparation->range_ready_id_ != m_appliedRangeRequest)
            {
                range = m_preparation->range_ready_;
                ordinal = m_preparation->range_ready_ordinal_;
                request = m_preparation->range_ready_id_;
            }
        }
        if (range)
        {
            reset_graph();
            m_viewValid = false;
            m_viewAutoDefault = true;
            adopt(std::move(range));
            select_latest();
            m_recordingFirstOrdinal = ordinal;
            m_appliedRangeRequest = request;
        }
    }

    bool capture_reader::preparation_pending() const
    {
        if (!m_preparation)
        {
            return false;
        }
        std::unique_lock lock(m_preparation->mutex_, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return true;
        }
        const auto& state = *m_preparation;
        return (state.range_request_ && state.range_done_ != state.range_request_->id_) ||
            (state.window_request_ && state.window_done_ != state.window_request_->id_) ||
            (state.selection_request_ && state.selection_done_ != state.selection_request_->id_);
    }

    bool capture_reader::preparation_failed() const
    {
        if (!m_preparation)
        {
            return static_cast<bool>(m_recording);
        }
        std::unique_lock lock(m_preparation->mutex_, std::try_to_lock);
        return lock.owns_lock() && (m_preparation->range_failed_ || m_preparation->window_failed_ ||
                                   m_preparation->selection_failed_);
    }

    std::pair<profile_tick, profile_tick> capture_reader::window_ticks() const
    {
        m_viewWindowFirst = graph_first();
        m_viewWindowLast = graph_last();
        if (!m_capture)
        {
            return {};
        }
        const auto frames = m_capture->frames();
        const auto first = std::lower_bound(frames.begin(), frames.end(), m_viewWindowFirst,
            [](const frame_record& frame, std::uint32_t value) { return frame.engine_frame < value; });
        const auto last = std::upper_bound(first, frames.end(), m_viewWindowLast,
            [](std::uint32_t value, const frame_record& frame) { return value < frame.engine_frame; });
        if (first == last)
        {
            return {};
        }
        profile_tick low = first->tick_begin;
        profile_tick high = first->tick_end;
        // 이벤트를 읽지 않고 작은 프레임 메타데이터 구간만 훑는다. 과거
        // 파일의 시간 경계가 겹쳐 있어도 집계와 같은 최소/최대 범위를 쓴다.
        for (auto frame = first + 1; frame != last; ++frame)
        {
            low = (std::min)(low, frame->tick_begin);
            high = (std::max)(high, frame->tick_end);
        }
        return {low, high};
    }

    bool capture_reader::sync(capture_session_ptr latest)
    {
        if (latest.get() == m_capture.get())
        {
            // 같은 것이면 손대지 않는다. 다시 adopt 하면 선택과 시야가 매 프레임
            // 초기화되고 접은 결과가 매 프레임 버려진다.
            return false;
        }

        if (!latest)
        {
            // 서비스가 내놓은 것이 없다고 해서 보던 것을 버리지는 않는다. 놓는 것은
            // Clear 가 reset() 으로 명시하는 일이다.
            return false;
        }

        if ((m_capture || m_recording) && !m_liveFollow)
        {
            // ★ 따라가지 않기로 했으면 새로 얼린 것이 와도 보던 것을 지킨다.
            //   스파이크를 붙잡아 둔 손에서 빠져나가면 그 체크박스는 거짓말이 된다.
            return false;
        }

        if (m_recording)
        {
            cancel_preparation();
            m_recording.reset();
        }
        adopt(std::move(latest));
        return true;
    }

    void capture_reader::adopt(capture_session_ptr capture)
    {
        const profile_tick previousSpan = m_viewValid && m_viewEnd > m_viewBegin
            ? m_viewEnd - m_viewBegin : 0;
        const bool previousWholeWindow = m_viewValid && m_viewSpansWholeWindow;
        m_capture = std::move(capture);
        m_aggregateValid = false;
        m_windowAggregateValid = false;

        if (!m_capture || m_capture->frame_count() == 0)
        {
            m_capture.reset();
            m_viewValid = false;
            m_availableFirst = m_availableLast = 0;
            m_selectedFirst = m_selectedLast = 0;
            return;
        }

        const std::span<const frame_record> frames = m_capture->frames();
        m_availableFirst = frames.front().engine_frame;
        m_availableLast = frames.back().engine_frame;

        if (m_liveFollow)
        {
            m_selectedFirst = m_selectedLast = m_availableLast;

            // ★ 따라가는 동안에는 그래프도 **끝을 붙잡는다.** 창 너비는
            //   그대로 두고 오른쪽 끝만 최신에 맞춘다 — 확대해 둔 것이
            //   새 스냅샷마다 풀리면 확대가 뜻이 없다.
            if (0 != m_graphCount)
            {
                const std::uint32_t span = m_graphCount;
                m_graphFirst = (m_availableLast + 1 > span)
                    ? (m_availableLast + 1 - span) : m_availableFirst;
            }
            clamp_graph();
            if (m_viewAutoDefault)
            {
                m_viewValid = false;
                return;
            }
            if (previousSpan > 0)
            {
                const auto [low, high] = window_ticks();
                m_viewEnd = high;
                m_viewBegin = previousWholeWindow ? low
                    : (m_viewEnd > previousSpan ? m_viewEnd - previousSpan : 0);
                m_viewValid = true;
                clamp_view();
            }
            return;
        }

        // ★ 따라가지 않기로 했으면 보던 자리를 지킨다. 다만 rolling ring 이
        //   그 프레임을 버렸으면 지킬 수가 없으므로 범위 안으로 자른다 —
        //   조용히 최신으로 점프하면 "붙잡아 뒀다" 는 약속이 깨진다.
        clamp_selection();

        // ★ 그래프도 같다. 보던 구간이 링에서 밀려나면 자르되, 최신으로
        //   점프시키지는 않는다 — 뒤로 굴려 놓고 보던 사람의 손에서 자료가
        //   빠져나가는 것이 이 창이 있는 까닭과 정반대다.
        clamp_graph();
        if (m_viewValid)
        {
            clamp_view();
        }
    }

    // ── 프레임 그래프의 창(§7.2) ────────────────────────────────────────
    //
    // 0 은 "아직 세우지 않았다" 이고 보존 구간 전체를 뜻한다. 캡처가 없으면
    // 둘 다 0 이라 그리는 쪽이 아무것도 안 그린다.
    std::uint32_t capture_reader::graph_first() const
    {
        if (!m_capture)
        {
            return 0;
        }
        return (0 == m_graphCount) ? m_availableFirst : m_graphFirst;
    }

    std::uint32_t capture_reader::graph_count() const
    {
        if (!m_capture)
        {
            return 0;
        }
        const std::uint32_t available = m_availableLast - m_availableFirst + 1;
        return (0 == m_graphCount) ? available : m_graphCount;
    }

    std::uint32_t capture_reader::graph_last() const
    {
        const std::uint32_t count = graph_count();
        return (0 == count) ? 0 : (graph_first() + count - 1);
    }

    void capture_reader::set_graph_span(std::uint32_t frames)
    {
        if (!m_capture || 0 == frames)
        {
            return;
        }
        const std::uint32_t anchor = graph_last();
        m_graphCount = frames;

        // ★ 폭이 바뀔 때 움직이는 것은 **왼쪽 끝**이다. 오른쪽 끝을 붙잡아야
        //   창을 넓혀도 보고 있던 최신 프레임이 제자리에 남는다.
        m_graphFirst = (anchor + 1 > frames) ? (anchor + 1 - frames) : m_availableFirst;
        clamp_graph();
    }

    void capture_reader::pan_graph(std::int32_t delta_frames)
    {
        if (!m_capture || 0 == delta_frames)
        {
            return;
        }
        const std::uint32_t before = graph_first();
        const std::uint32_t count = graph_count();
        const std::int64_t first = static_cast<std::int64_t>(before) + delta_frames;

        m_graphCount = count;
        m_graphFirst = (first < static_cast<std::int64_t>(m_availableFirst))
            ? m_availableFirst : static_cast<std::uint32_t>(first);
        clamp_graph();

        // ★ 고른 프레임을 **같은 만큼** 민다. 굴리는 것은 "지금 보고 있는
        //   자리" 하나여야 한다 — 그래프 창만 움직이고 선택이 제자리면 아래
        //   타임라인은 그대로라서, 스크롤이 위쪽 그림만 흔드는 것처럼 보인다.
        //
        //   경계에서 창이 덜 움직였으면 선택도 덜 움직여야 한다. 그래서 청한
        //   양이 아니라 **실제로 움직인 양**을 쓴다.
        const std::int64_t applied =
            static_cast<std::int64_t>(m_graphFirst) - static_cast<std::int64_t>(before);
        if (applied != 0)
        {
            m_viewAutoDefault = false;
        }
        shift_selection(applied);

        // ★ 뒤로 굴렸으면 따라가기를 끈다. 안 끄면 다음 스냅샷이 창을 최신으로
        //   되돌려서, 손으로 굴린 것이 한 프레임 만에 사라진다.
        const std::uint32_t lastFirst = (m_availableLast + 1 > m_graphCount)
            ? (m_availableLast + 1 - m_graphCount) : m_availableFirst;
        // 파일 안에서 끝까지 이동해도 라이브로 돌아가지는 않는다.
        set_live_follow(!m_recording && m_graphFirst >= lastFirst);
    }

    // 고른 구간을 통째로 옮긴다. 폭은 지킨다 — 굴리다가 선택이 넓어지거나
    // 좁아지면 아래 표의 수가 조용히 달라진다.
    void capture_reader::shift_selection(std::int64_t delta)
    {
        if (0 == delta)
        {
            return;
        }
        const std::int64_t first = static_cast<std::int64_t>(m_selectedFirst) + delta;
        const std::int64_t last = static_cast<std::int64_t>(m_selectedLast) + delta;
        const std::int64_t floor = static_cast<std::int64_t>(m_availableFirst);

        m_selectedFirst = static_cast<std::uint32_t>((std::max)(first, floor));
        m_selectedLast = static_cast<std::uint32_t>((std::max)(last, floor));
        clamp_selection();
    }

    void capture_reader::reset_graph()
    {
        m_graphFirst = 0;
        m_graphCount = 0;
    }

    void capture_reader::clamp_graph()
    {
        clamp_graph_range();

        // ★ 창이 움직였으면 시야를 다시 앉힌다. 안 하면 타임라인이 지난 창의
        //   tick 을 그대로 들고 있어서, 굴린 뒤 빈 화면이 나온다.
        if (m_viewValid
            && (m_viewWindowFirst != graph_first()
                || m_viewWindowLast != graph_last()))
        {
            rebase_view_to_window();
        }
    }

    void capture_reader::clamp_graph_range()
    {
        if (!m_capture || 0 == m_graphCount)
        {
            return;
        }

        const std::uint32_t available = m_availableLast - m_availableFirst + 1;
        if (m_graphCount > available)
        {
            m_graphCount = available;
        }
        if (m_graphCount < kMinimumGraphFrames)
        {
            m_graphCount = (available < kMinimumGraphFrames) ? available : kMinimumGraphFrames;
        }

        if (m_graphFirst < m_availableFirst)
        {
            m_graphFirst = m_availableFirst;
        }

        // ★ 오른쪽 끝을 넘지 않는다. 넘으면 빈 칸을 그리게 되고, 그때
        //   사용자는 "그 프레임들이 사라졌다" 로 읽는다.
        const std::uint32_t lastFirst = m_availableLast - m_graphCount + 1;
        if (m_graphFirst > lastFirst)
        {
            m_graphFirst = lastFirst;
        }
    }

    void capture_reader::reset()
    {
        cancel_preparation();
        m_recording.reset();
        m_recordingFirstOrdinal = 0;
        m_capture.reset();
        m_availableFirst = m_availableLast = 0;
        m_selectedFirst = m_selectedLast = 0;
        m_aggregateValid = false;
        m_windowAggregateValid = false;
        m_viewValid = false;
        m_viewAutoDefault = true;
        reset_graph();
    }

    void capture_reader::open(capture_session_ptr capture)
    {
        cancel_preparation();
        m_recording.reset();
        // 파일을 보는 동안은 라이브를 따라가지 않는다 — 다음 sync() 가 덮는다.
        m_liveFollow = false;

        // 라이브의 그래프 창은 이 파일에서 뜻이 없다. 비워 두면 다음
        // set_graph_span 이 파일의 최신 끝에 창을 세운다.
        reset_graph();
        m_viewValid = false;
        m_viewAutoDefault = true;

        adopt(std::move(capture));

        // 파일의 최신 프레임을 고른다. 보던 선택을 이어 가면 남의 번호가
        // 파일 범위 가장자리에 잘려 붙는다.
        select_latest();
    }

    void capture_reader::set_live_follow(bool value)
    {
        if (m_liveFollow == value)
        {
            return;
        }
        m_liveFollow = value;

        // 켜는 순간 최신으로 간다. 켜 두고 아무 일도 안 일어나면 토글이
        // 다음 캡처까지 아무 뜻도 없어 보인다.
        if (m_liveFollow && m_capture)
        {
            select_latest();
        }
    }

    void capture_reader::select_range(std::uint32_t first, std::uint32_t last)
    {
        if (last < first)
        {
            std::swap(first, last);
        }

        const std::uint32_t previousFirst = m_selectedFirst;
        const std::uint32_t previousLast = m_selectedLast;

        m_selectedFirst = first;
        m_selectedLast = last;
        clamp_selection();

        if (m_selectedFirst != previousFirst || m_selectedLast != previousLast)
        {
            // ★ 시야는 건드리지 않는다. 기준이 선택에서 창으로 옮겨졌으므로
            //   다른 프레임을 골라도 타임라인이 그리는 범위는 그대로고,
            //   여기서 되돌리면 확대해 둔 것이 클릭 한 번에 풀린다.
            m_aggregateValid = false;
        }
    }

    void capture_reader::select_latest()
    {
        if (!m_capture)
        {
            return;
        }
        select_range(m_availableLast, m_availableLast);
    }

    void capture_reader::focus_frame(std::uint32_t frame)
    {
        if (!m_capture)
        {
            return;
        }
        const frame_record* target = m_capture->find_frame(frame);
        if (!target)
        {
            return;
        }
        ensure_view();
        if (target->tick_begin >= m_viewBegin && target->tick_end <= m_viewEnd)
        {
            return;
        }
        const profile_tick span = m_viewEnd - m_viewBegin;
        const profile_tick center = target->tick_begin + (target->tick_end - target->tick_begin) / 2;
        m_viewBegin = center > span / 2 ? center - span / 2 : 0;
        m_viewEnd = m_viewBegin + span;
        m_viewAutoDefault = false;
        clamp_view();
    }

    void capture_reader::clamp_selection()
    {
        if (!m_capture)
        {
            m_selectedFirst = m_selectedLast = 0;
            return;
        }

        m_selectedFirst = std::clamp(m_selectedFirst, m_availableFirst, m_availableLast);
        m_selectedLast = std::clamp(m_selectedLast, m_availableFirst, m_availableLast);
        if (m_selectedLast < m_selectedFirst)
        {
            m_selectedLast = m_selectedFirst;
        }
    }

    // ── Timeline 의 가로 시야 ────────────────────────────────────────────────
    //
    // 최소 폭. 이 아래로 좁히면 스팬이 픽셀 하나에 뭉개지고, tick 이 정수라
    // 반올림이 시야를 뒤집을 수 있다(begin > end).
    namespace
    {
        constexpr profile_tick kMinimumViewTicks = 16;
    }

    // ★ 기준이 선택에서 **창**으로 옮겨졌다. 고른 한 프레임을 기준으로 삼으면
    //   위 그래프가 244 프레임을 보여 주는 동안 아래 타임라인은 1.6 ms 짜리
    //   한 칸만 그린다.
    void capture_reader::ensure_view() const
    {
        if (m_viewValid)
        {
            return;
        }

        const auto [low, high] = window_ticks();
        m_viewEnd = high;
        if (m_viewEnd <= low)
        {
            m_viewEnd = low + kMinimumViewTicks;
        }
        const profile_tick frequency = m_capture ? m_capture->environment().ticks_per_second : 0;
        const profile_tick preferred = (std::max)(frequency / 10, kMinimumViewTicks);
        const profile_tick span = (std::min)(m_viewEnd - low, preferred);
        m_viewBegin = m_viewEnd - span;
        m_viewValid = true;
        m_viewSpansWholeWindow = span >= m_viewEnd - low;
    }

    // 창이 미끄러졌을 때 시야를 어떻게 할 것인가.
    //
    // ★ 확대해 두지 않았으면 **따라간다.** 확대해 뒀으면 보던 자리를 지키고
    //   범위 안으로만 자른다 — 굴릴 때마다 확대가 풀리면 확대가 뜻이 없다.
    void capture_reader::rebase_view_to_window() const
    {
        if (!m_viewValid)
        {
            return;
        }

        if (m_viewSpansWholeWindow)
        {
            m_viewValid = false;
            return;
        }
        clamp_view();
    }

    void capture_reader::clamp_view() const
    {
        const auto [low, end] = window_ticks();
        const profile_tick high = end > low ? end : low + kMinimumViewTicks;

        if (m_viewEnd <= m_viewBegin || (m_viewEnd - m_viewBegin) < kMinimumViewTicks)
        {
            m_viewEnd = m_viewBegin + kMinimumViewTicks;
        }

        profile_tick span = m_viewEnd - m_viewBegin;
        const profile_tick full = high - low;
        if (span > full)
        {
            span = full;
        }

        // 구간 밖으로 나가지 않는다. 나갈 수 있으면 빈 화면을 보게 되고,
        // 그때 사용자는 계측이 없다고 읽는다.
        if (m_viewBegin < low)
        {
            m_viewBegin = low;
        }
        if (m_viewBegin + span > high)
        {
            m_viewBegin = high - span;
        }
        m_viewEnd = m_viewBegin + span;

        // 확대해 뒀는가. 창이 미끄러질 때 따라갈지 자리를 지킬지가 여기서
        // 갈린다 — 그래서 자르는 자리에서 한 번만 적는다.
        m_viewSpansWholeWindow = (span >= full);
    }

    profile_tick capture_reader::view_begin() const
    {
        ensure_view();
        return m_viewBegin;
    }

    profile_tick capture_reader::view_end() const
    {
        ensure_view();
        return m_viewEnd;
    }

    profile_tick capture_reader::view_span() const
    {
        ensure_view();
        return (m_viewEnd > m_viewBegin) ? (m_viewEnd - m_viewBegin) : 0;
    }

    void capture_reader::reset_view()
    {
        m_viewAutoDefault = false;
        const auto [low, high] = window_ticks();
        m_viewBegin = low;
        m_viewEnd = (std::max)(high, m_viewBegin + kMinimumViewTicks);
        m_viewValid = true;
        m_viewSpansWholeWindow = true;
    }

    void capture_reader::seek_view(profile_tick begin)
    {
        if (!m_capture || m_capture->frames().empty())
        {
            return;
        }
        m_viewAutoDefault = false;
        ensure_view();
        const profile_tick span = m_viewEnd - m_viewBegin;
        const auto frames = m_capture->frames();
        const profile_tick captureBegin = frames.front().tick_begin;
        const profile_tick captureEnd = frames.back().tick_end;
        const profile_tick lastBegin = captureEnd > captureBegin + span
            ? captureEnd - span : captureBegin;
        begin = (std::clamp)(begin, captureBegin, lastBegin);
        const profile_tick center = begin + span / 2;
        auto found = std::find_if(frames.begin(), frames.end(),
            [center](const frame_record& frame) { return frame.tick_end >= center; });
        if (found == frames.end())
        {
            found = frames.end() - 1;
        }
        const std::uint32_t count = graph_count();
        if (count > 0)
        {
            m_graphCount = count;
            const std::uint32_t target = found->engine_frame;
            m_graphFirst = target > count / 2 ? target - count / 2 : m_availableFirst;
            clamp_graph();
        }
        m_viewBegin = begin;
        m_viewEnd = begin + span;
        m_viewValid = true;
        clamp_view();
        set_live_follow(false);
        select_frame(found->engine_frame);
    }

    void capture_reader::zoom_view(double factor, profile_tick pivot)
    {
        ensure_view();
        if (!(factor > 0.0))
        {
            return;
        }
        m_viewAutoDefault = false;

        const profile_tick span = (m_viewEnd > m_viewBegin)
            ? (m_viewEnd - m_viewBegin) : kMinimumViewTicks;

        // pivot 을 제자리에 두려면 그것이 시야에서 차지하는 비율을 지켜야 한다.
        const profile_tick clampedPivot = (pivot < m_viewBegin) ? m_viewBegin
            : ((pivot > m_viewEnd) ? m_viewEnd : pivot);
        const double ratio = static_cast<double>(clampedPivot - m_viewBegin)
            / static_cast<double>(span);

        double scaled = static_cast<double>(span) * factor;
        if (scaled < static_cast<double>(kMinimumViewTicks))
        {
            scaled = static_cast<double>(kMinimumViewTicks);
        }
        const profile_tick nextSpan = static_cast<profile_tick>(scaled);

        const double nextBegin = static_cast<double>(clampedPivot)
            - ratio * static_cast<double>(nextSpan);
        m_viewBegin = (nextBegin > 0.0) ? static_cast<profile_tick>(nextBegin) : 0;
        m_viewEnd = m_viewBegin + nextSpan;
        clamp_view();
    }

    void capture_reader::pan_view(std::int64_t delta_ticks)
    {
        if (delta_ticks == 0)
        {
            return;
        }
        ensure_view();
        m_viewAutoDefault = false;
        const profile_tick span = (m_viewEnd > m_viewBegin)
            ? (m_viewEnd - m_viewBegin) : kMinimumViewTicks;

        if (delta_ticks < 0)
        {
            const profile_tick back = static_cast<profile_tick>(-delta_ticks);
            m_viewBegin = (m_viewBegin > back) ? (m_viewBegin - back) : 0;
        }
        else
        {
            m_viewBegin += static_cast<profile_tick>(delta_ticks);
        }
        m_viewEnd = m_viewBegin + span;
        clamp_view();
    }

    const frame_aggregate& capture_reader::window_aggregate() const
    {
        if (m_preparation)
        {
            const auto ready = prepared_window();
            return ready ? ready->aggregate_ : m_windowAggregate;
        }

        const std::uint32_t first = graph_first();
        const std::uint32_t last = graph_last();

        if (m_windowAggregateValid
            && m_windowAggregateFirst == first && m_windowAggregateLast == last)
        {
            return m_windowAggregate;
        }

        if (m_capture)
        {
            m_windowAggregate =
                aggregate_frames(*m_capture, first, last, aggregate_scope::spans_only);
            ++m_foldCount;
        }
        else
        {
            m_windowAggregate = frame_aggregate{};
        }
        m_windowAggregateFirst = first;
        m_windowAggregateLast = last;
        m_windowAggregateValid = true;
        return m_windowAggregate;
    }

    const frame_aggregate& capture_reader::aggregate() const
    {
        if (m_preparation)
        {
            request_preparation(false);
            std::shared_ptr<const detail::profile_reader_impl::prepared_selection> ready;
            {
                std::unique_lock lock(m_preparation->mutex_, std::try_to_lock);
                if (lock.owns_lock())
                {
                    ready = m_preparation->selection_ready_;
                }
            }
            if (ready)
            {
                m_preparedSelection = std::move(ready);
            }
            // 다른 탭은 capture()로 이름을 푼다. 다른 원본 또는 다른 선택의
            // 집계를 새 이름·범위와 함께 내놓지 않는다.
            return m_preparedSelection && m_preparedSelection->capture_ == m_capture &&
                m_preparedSelection->aggregate_.frame_begin() == m_selectedFirst &&
                m_preparedSelection->aggregate_.frame_end() == m_selectedLast + 1
                ? m_preparedSelection->aggregate_ : m_aggregate;
        }

        if (m_aggregateValid)
        {
            return m_aggregate;
        }

        if (m_capture)
        {
            m_aggregate = aggregate_frames(*m_capture, m_selectedFirst, m_selectedLast);
            ++m_foldCount;
        }
        else
        {
            m_aggregate = frame_aggregate{};
        }
        m_aggregateValid = true;
        return m_aggregate;
    }
}
