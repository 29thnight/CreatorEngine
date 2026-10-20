#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"

#include <iostream>

namespace
{
    std::size_t checks = 0;
    thread_local bool count_allocations = false;
    thread_local std::size_t heap_allocations = 0;

    void require(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    ce::profile_rhi_context context(std::uint64_t id = 1)
    {
        ce::profile_rhi_context result;
        result.recording_id = (std::uint64_t{1} << 45) + id;
        result.source_engine_frame = (std::uint64_t{1} << 47) + 11;
        result.source_engine_frame_available = true;
        result.frame_id = (std::uint64_t{1} << 48) + 22;
        result.view_id = (std::uint64_t{1} << 49) + 33;
        result.device_incarnation = (std::uint64_t{1} << 50) + 44;
        result.backend_generation = (std::uint64_t{1} << 51) + 55;
        result.queue_id = UINT32_MAX - 2;
        result.target_index = 7;
        result.queue_kind = 2;
        return result;
    }

    struct wire
    {
        std::vector<std::byte> bytes;
        template<class T>
        void put(T value)
        {
            const auto at = bytes.size();
            bytes.resize(at + sizeof(T));
            std::memcpy(bytes.data() + at, &value, sizeof(T));
        }
    };
    struct reader
    {
        std::span<const std::byte> bytes;
        std::size_t at = 0;
        template<class T>
        bool get(T& value)
        {
            if (sizeof(T) > bytes.size() - at)
            {
                return false;
            }
            std::memcpy(&value, bytes.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }
    };

    template<class T>
    T read_at(const std::vector<std::byte>& bytes, std::size_t at)
    {
        require(at <= bytes.size() && sizeof(T) <= bytes.size() - at, "fixture wire bounds");
        T value{};
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        return value;
    }
    template<class T>
    void patch(std::vector<std::byte>& bytes, std::size_t at, T value)
    {
        require(at <= bytes.size() && sizeof(T) <= bytes.size() - at, "fixture patch bounds");
        std::memcpy(bytes.data() + at, &value, sizeof(T));
    }
    std::uint32_t crc(std::span<const std::byte> bytes)
    {
        std::uint32_t result = UINT32_MAX;
        for (auto byte : bytes)
        {
            result ^= std::to_integer<std::uint8_t>(byte);
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                result = (result >> 1) ^ ((result & 1) ? 0xedb88320u : 0u);
            }
        }
        return result ^ UINT32_MAX;
    }

    void set_snapshot_version(std::vector<std::byte>& bytes, std::size_t base, std::uint32_t version)
    {
        patch(bytes, base + 8, version);
        const auto count = read_at<std::uint32_t>(bytes, base + 12);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto at = base + 16 + index * 32;
            if (read_at<std::uint32_t>(bytes, at) == 7)
            {
                patch(bytes, at + 4, version == 8 ? 3u : version == 6 ? 2u : 1u);
            }
        }
    }

    ce::profile_event event(bool gpu = false)
    {
        ce::profile_event value;
        value.tick_begin = 110;
        value.tick_end = 170;
        value.marker = gpu ? 2 : 1;
        value.frame = 9;
        value.thread_slot = gpu ? 1 : 0;
        value.depth = 3;
        value.flags = gpu ? ce::event_flags::gpu_span : ce::event_flags::none;
        if (gpu)
        {
            value.queue = 5;
            value.submission = UINT32_MAX - 9;
            value.view = UINT16_MAX - 8;
        }
        else
        {
            value.cpu = {13, 17, 19};
        }
        return value;
    }

    ce::capture_session_ptr capture(bool metadata)
    {
        ce::frame_record frame;
        frame.engine_frame = 9;
        frame.tick_begin = 100;
        frame.tick_end = 200;
        for (bool gpu : {false, true})
        {
            const auto value = event(gpu);
            frame.events.push_back(value);
            if (metadata)
            {
                frame.render_measurements.push_back(ce::make_rhi_measurement(value, context(gpu ? 2 : 1)));
            }
        }
        std::vector<ce::frame_record> frames;
        frames.push_back(std::move(frame));
        return std::make_shared<const ce::capture_session>(
            std::move(frames),
            std::vector<ce::thread_info>{{"CPU", 10, 0, ce::track_kind::command_thread, 0},
                                         {"GPU", 11, 1, ce::track_kind::gpu_graphics, 0}},
            std::vector<ce::capture_marker>{{},
                                            {"RHICommandTranslate", "", 0, ce::marker_kind::cpu_scope},
                                            {"GPU.Pass", "", 0, ce::marker_kind::gpu_span}},
            ce::capture_environment{1000000}, true, 0);
    }

    void verify_context_and_wire()
    {
        static_assert(sizeof(ce::profile_event) == 64);
        const auto outer = context();
        auto inner = context(2);
        inner.target_index = UINT32_MAX;
        inner.source_engine_frame_available = false;
        inner.source_engine_frame = 0;
        require(outer.valid() && inner.valid(), "valid context rejected");
        const auto original = ce::current_rhi_context;
        {
            ce::profile_rhi_context_scope scope(outer);
            require(ce::current_rhi_context == outer, "TLS outer context lost");
            {
                ce::profile_rhi_context_scope nested(inner);
                require(ce::current_rhi_context == inner, "TLS nested context lost");
            }
            require(ce::current_rhi_context == outer, "TLS restoration lost");
            std::thread worker(
                [&] { require(ce::current_rhi_context == ce::profile_rhi_context{}, "TLS leaked to worker"); });
            worker.join();
        }
        require(ce::current_rhi_context == original, "TLS final restoration lost");
        for (bool gpu : {false, true})
        {
            auto sample = ce::make_rhi_measurement(event(gpu), gpu ? inner : outer);
            wire encoded;
            ce::encode_render_measurement(encoded, sample);
            require(encoded.bytes.size() == 100, "RHI wire widened existing sidecar record");
            reader input{encoded.bytes};
            ce::profile_render_measurement decoded;
            require(ce::decode_render_measurement(input, decoded, true, true) && input.at == 100,
                    "RHI wire roundtrip failed");
            require(decoded.rhi == sample.rhi && decoded.rhi_event_frame == sample.rhi_event_frame &&
                        decoded.rhi_thread_slot == sample.rhi_thread_slot && decoded.rhi_depth == sample.rhi_depth &&
                        decoded.rhi_event_flags == sample.rhi_event_flags && decoded.event_view == sample.event_view &&
                        decoded.event_submission == sample.event_submission && decoded.queue == sample.queue,
                    "RHI identity or exact event key was narrowed");
            reader old{encoded.bytes};
            require(!ce::decode_render_measurement(old, decoded, true, false), "old wire version accepted RHI axis");
            for (std::size_t cut = 0; cut < encoded.bytes.size(); ++cut)
            {
                reader truncated{std::span(encoded.bytes).first(cut)};
                require(!ce::decode_render_measurement(truncated, decoded), "truncated RHI payload accepted");
            }
            for (const auto [offset, byte] :
                 std::array<std::pair<std::size_t, std::uint8_t>, 4>{{{33, 0x80}, {97, 3}, {98, 2}, {99, 1}}})
            {
                auto corrupted = encoded.bytes;
                corrupted[offset] = static_cast<std::byte>(byte);
                reader bad{corrupted};
                require(!ce::decode_render_measurement(bad, decoded),
                        "invalid RHI flags/kind/availability/reserved accepted");
            }
        }
        auto invalid = outer;
        invalid.source_engine_frame_available = false;
        require(!invalid.valid(), "unavailable source frame retained a fabricated identity");
        invalid = outer;
        invalid.queue_id = 0;
        require(!invalid.valid(), "partial queue identity accepted");
    }

    void verify_stream_and_projection()
    {
        auto pool = std::make_shared<ce::chunk_pool>();
        pool->initialize(4, 4);
        ce::thread_stream stream(*pool, {"CPU", 0, 0, ce::track_kind::command_thread, 0});
        stream.set_generation(4);
        auto expected = context();
        stream.begin_scope(1, 50, 7, {13, 17, 19}, 4, &expected);
        expected = context(88); // The open scope must own the old value.
        ce::profile_event projected[ce::kMaxScopeDepth]{};
        ce::profile_rhi_context identities[ce::kMaxScopeDepth]{};
        require(stream.snapshot_open_scopes(4, 90, projected, ce::kMaxScopeDepth, identities) == 1 &&
                    identities[0] == context() && projected[0].cpu.task == 19,
                "published RHI projection lost identity or CPU owner");
        stream.end_scope(150);
        stream.begin_scope(1, 151, 8, {13, 17, 20});
        stream.end_scope(160);
        stream.publish_frame();
        ce::capture_ring ring;
        ring.configure(4, 1 << 20);
        ring.close_frame(7, 0, 100);
        ring.ingest(pool->take_sealed(), pool, 4, 100);
        ring.close_frame(8, 100, 200);
        const std::array<ce::thread_info, 1> threads{{{"CPU", 0, 0, ce::track_kind::command_thread, 0}}};
        const auto frozen = ring.freeze(threads, {1000000}, true, 0);
        const auto* frame = frozen->find_frame(8);
        require(frame && frame->events.size() == 2 && frame->render_measurements.size() == 1,
                "cross-frame RHI sidecar did not follow the CPU end tick");
        require(frame->events[0].frame == 7 && frame->events[0].cpu.session == 13 && frame->events[0].cpu.tick == 17 &&
                    frame->events[0].cpu.task == 19,
                "RHI metadata changed CPU event semantics");
        require(frame->rhi_context_for(frame->events[0]) && *frame->rhi_context_for(frame->events[0]) == context(),
                "cross-frame exact-key join failed");
        require(!frame->rhi_context_for(frame->events[1]), "ordinary scope inherited stale RHI identity");
        ce::frame_record collision = *frame;
        collision.render_measurements.push_back(collision.render_measurements.front());
        require(!collision.rhi_context_for(collision.events[0]), "ambiguous identity selected latest");

        for (std::uint32_t depth = 0; depth < ce::kMaxScopeDepth; ++depth)
        {
            const auto rhi = context(depth + 1);
            stream.begin_scope(1, 210 + depth, 9, {}, 4, &rhi);
        }
        require(stream.claim_open_scopes(4, 300, projected, ce::kMaxScopeDepth, identities) == ce::kMaxScopeDepth,
                "Stop could not claim full bounded open stack");
        for (std::uint32_t depth = 0; depth < ce::kMaxScopeDepth; ++depth)
        {
            require(identities[depth] == context(depth + 1) &&
                        ce::has_flag(projected[depth].flags, ce::event_flags::truncated_end),
                    "claimed RHI context lost");
            stream.end_scope(310);
        }
        stream.publish_frame();
        require(pool->take_sealed() == nullptr, "claimed scopes were emitted again by owner");
        stream.begin_scope(1, 320, 10, {});
        require(stream.snapshot_open_scopes(4, 330, projected, 1, identities) == 1 && !identities[0].valid(),
                "reused ordinary stack slot retained published RHI token");
        stream.end_scope(340);
        stream.publish_frame();
        pool->release(pool->take_sealed());

        ce::gpu_span_context gpu;
        gpu.submission = UINT32_MAX - 9;
        gpu.view = UINT16_MAX - 8;
        gpu.queue = 5;
        gpu.generation = 4;
        gpu.rhi = context(2);
        stream.write_span(2, 150, 180, 8, 3, gpu);
        stream.publish_frame();
        auto* page = pool->take_sealed();
        require(page && page->count == 1 && page->render_measurement_count == 1 &&
                    page->render_measurements[0].rhi == gpu.rhi &&
                    page->render_measurements[0].rhi_event_flags ==
                        static_cast<std::uint8_t>(ce::event_flags::gpu_span) &&
                    page->events[0].cpu.session == 0,
                "GPU RHI identity missing without render-quality provenance");
        pool->release(page);
    }

    void verify_bounded_pages()
    {
        auto pool = std::make_shared<ce::chunk_pool>();
        pool->initialize(1, 1);
        ce::thread_stream stream(*pool, {"bounded", 0, 0, ce::track_kind::command_thread, 0});
        stream.set_generation(1);
        const auto rhi = context();
        for (unsigned index = 0; index <= ce::kRenderMeasurementsPerChunk; ++index)
        {
            stream.begin_scope(1, index * 10 + 1, 1, {}, 1, &rhi);
            stream.end_scope(index * 10 + 2);
        }
        stream.publish_frame();
        auto* page = pool->take_sealed();
        require(page && !page->next && page->count == ce::kRenderMeasurementsPerChunk &&
                    page->render_measurement_count == page->count && stream.dropped_events() == 1,
                "bounded sidecar capacity lost paired-event/drop accounting");
        pool->release(page);
        const auto before = heap_allocations;
        count_allocations = true;
        for (unsigned index = 0; index < 128; ++index)
        {
            stream.begin_scope(1, index * 10 + 1000, 2, {}, 1, &rhi);
            stream.end_scope(index * 10 + 1001);
            stream.publish_frame();
            pool->release(pool->take_sealed());
        }
        count_allocations = false;
        require(heap_allocations == before, "warm bounded RHI producer allocated");
    }

    void verify_codec_and_versions(const std::filesystem::path& directory)
    {
        const auto source = capture(true);
        const auto bytes = ce::encode_capture(*source);
        require(read_at<std::uint32_t>(bytes, 8) == 8, "snapshot format not versioned");
        const auto decoded = ce::decode_capture(bytes);
        require(decoded && (*decoded)->frame_count() == 1, "snapshot RHI roundtrip failed");
        for (std::size_t index = 0; index < 2; ++index)
        {
            const auto& frame = (*decoded)->frames()[0];
            require(frame.rhi_context_for(frame.events[index]) &&
                        *frame.rhi_context_for(frame.events[index]) == context(index + 1),
                    "snapshot exact RHI join failed");
        }
        for (std::uint32_t version : {4u, 6u})
        {
            auto old = ce::encode_capture(*capture(false));
            set_snapshot_version(old, 0, version);
            require(bool(ce::decode_capture(old)), "legacy snapshot rejected");
            auto illegal = bytes;
            set_snapshot_version(illegal, 0, version);
            require(!ce::decode_capture(illegal), "old snapshot version accepted new RHI axis");
        }
        const auto snapshot = directory / "rhi-snapshot.ceprof";
        require(bool(ce::save_capture(*source, snapshot)), "snapshot save failed");
        const auto indexed = ce::open_capture_recording(snapshot);
        require(indexed && (*indexed)->frame_count() == 1, "indexed snapshot failed");
        const auto selected = (*indexed)->load_range(0, 1);
        require(selected && (*selected)->frames()[0].rhi_context_for((*selected)->frames()[0].events[1]),
                "indexed snapshot selection lost GPU RHI key");
        for (bool metadata : {false, true})
        {
            const auto path = directory / (metadata ? "rhi-stream.ceprof" : "empty-stream.ceprof");
            auto writer = ce::continuous_capture_writer::start(path);
            require(bool(writer), "stream writer start failed");
            require((*writer)->append(capture(metadata)), "stream append failed");
            (*writer)->request_finalize(true, 0);
            (*writer)->wait();
            require((*writer)->status().state == ce::recording_state::finalized, "stream finalize failed");
            const auto opened = ce::open_capture_recording(path);
            require(opened && (*opened)->frame_count() == 1, "stream reopen failed");
            const auto range = (*opened)->load_range(0, 1);
            require(range && (*range)->frames()[0].render_measurements.size() == (metadata ? 2 : 0),
                    "stream selected window lost RHI metadata");
            std::ifstream file(path, std::ios::binary);
            std::vector<char> raw((std::istreambuf_iterator<char>(file)), {});
            std::vector<std::byte> stream(raw.size());
            std::memcpy(stream.data(), raw.data(), raw.size());
            require(read_at<std::uint32_t>(stream, 8) == 9, "continuous format not versioned");
            for (std::uint32_t version : {5u, 7u})
            {
                auto old = stream;
                patch(old, 8, version);
                for (std::size_t at = 16; at < old.size();)
                {
                    const auto kind = read_at<std::uint32_t>(old, at + 4);
                    const auto length = read_at<std::uint64_t>(old, at + 16);
                    if (kind == 1)
                    {
                        set_snapshot_version(old, at + 32, version - 1);
                        patch(old, at + 24, crc(std::span(old).subspan(at + 32, length)));
                        patch(old, at + 28, crc(std::span(old).subspan(at, 28)));
                    }
                    at += 32 + length;
                }
                const auto oldPath = directory / ("old-" + std::to_string(version) + ".ceprof");
                std::ofstream out(oldPath, std::ios::binary);
                out.write(reinterpret_cast<const char*>(old.data()), old.size());
                out.close();
                const auto oldOpened = ce::open_capture_recording(oldPath);
                require(bool(oldOpened) == !metadata, "old stream version failed compatibility/new-axis rejection");
            }
        }
    }

    void verify_actual_service()
    {
        const auto marker = ce::marker<"RHI.Context.Fixture">();
        ce::profiler_service service;
        ce::profiler_config config;
        config.chunk_count = 8;
        config.max_chunk_count = 32;
        config.live_capture_interval_ms = 0;
        config.stop_drain_timeout_ms = 0;
        service.initialize(config);
        service.register_thread("RHI fixture", ce::track_kind::command_thread, 0);
        const auto before = heap_allocations;
        count_allocations = true;
        for (unsigned index = 0; index < 4096; ++index)
        {
            ce::profile_rhi_context_scope identity(context());
            ce::profile_rhi_scope scope(service, marker);
        }
        count_allocations = false;
        require(heap_allocations == before, "recording-OFF RHI scope allocated");
        service.record(11);
        service.wait_until_idle();
        service.publish_frame(11);
        service.wait_until_idle();
        {
            ce::profile_context_scope cpu({13, 17, 19});
            ce::profile_rhi_context_scope identity(context());
            ce::profile_rhi_scope outer(service, marker);
            {
                ce::profile_scope ordinary(service, marker);
            }
            service.publish_frame(12);
            service.wait_until_idle();
        }
        service.publish_frame(13);
        service.wait_until_idle();
        service.pause();
        service.wait_until_idle();
        const auto recorded = service.capture();
        require(recorded && recorded->complete(), "actual service capture failed");
        unsigned linked = 0, unknown = 0;
        for (const auto& frame : recorded->frames())
        {
            for (const auto& value : frame.events)
            {
                if (value.marker != marker)
                {
                    continue;
                }
                const auto* rhi = frame.rhi_context_for(value);
                if (rhi)
                {
                    require(*rhi == context() && value.cpu.session == 13 && value.cpu.task == 19,
                            "actual service modified RHI/CPU identity");
                    ++linked;
                }
                else
                {
                    ++unknown;
                }
            }
        }
        require(linked == 1 && unknown == 1, "actual service linked ordinary scope or lost completed RHI scope");
        service.unregister_thread();
        service.shutdown();
    }

    void verify_stop_with_full_rhi_stack()
    {
        ce::profiler_service service;
        ce::profiler_config config;
        config.chunk_count = 8;
        config.max_chunk_count = 32;
        config.stop_drain_timeout_ms = 0;
        service.initialize(config);
        service.register_thread("RHI stop owner");
        service.record(71);
        service.wait_until_idle();
        const auto marker = ce::marker<"RHI.Context.Parked">();
        std::binary_semaphore opened(0), resume(0);
        std::thread worker(
            [&]
            {
                service.register_thread("RHI stop worker");
                for (std::uint32_t index = 0; index < ce::kMaxScopeDepth; ++index)
                {
                    const auto identity = context(index + 1);
                    service.begin_scope(marker, {13, 17, index + 1}, &identity);
                }
                opened.release();
                resume.acquire();
                for (std::uint32_t index = 0; index < ce::kMaxScopeDepth; ++index)
                {
                    service.end_scope();
                }
                service.unregister_thread();
            });
        opened.acquire();
        service.publish_frame(71);
        service.wait_until_idle();
        service.pause();
        service.wait_until_idle();
        const auto captured = service.capture();
        // Join before any assertion can throw so the fixture itself cannot strand
        // a producer after testing the collector's transfer of emission ownership.
        resume.release();
        worker.join();
        require(captured && !captured->complete() && captured->unacked_streams() == 1,
                "parked producer completeness was guessed");
        std::array<bool, ce::kMaxScopeDepth> seen{};
        std::size_t linked = 0;
        for (const auto& frame : captured->frames())
        {
            for (const auto& value : frame.events)
            {
                if (value.marker != marker)
                {
                    continue;
                }
                const auto* identity = frame.rhi_context_for(value);
                require(value.depth < seen.size() && !seen[value.depth] && identity &&
                            *identity == context(value.depth + 1) && value.cpu.task == value.depth + 1 &&
                            ce::has_flag(value.flags, ce::event_flags::truncated_end),
                        "Stop lost, duplicated, or mixed full-stack RHI sidecars");
                seen[value.depth] = true;
                ++linked;
            }
        }
        require(linked == ce::kMaxScopeDepth, "Stop failed to retain sidecars beyond one bounded page");
        service.unregister_thread();
        service.shutdown();
    }
} // namespace

void* operator new(std::size_t size)
{
    if (count_allocations)
    {
        ++heap_allocations;
    }
    if (void* result = std::malloc(size ? size : 1))
    {
        return result;
    }
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept
{
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept
{
    std::free(value);
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete[](void* value) noexcept
{
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept
{
    std::free(value);
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "expected fixture output directory");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        verify_context_and_wire();
        verify_stream_and_projection();
        verify_bounded_pages();
        verify_codec_and_versions(directory);
        verify_actual_service();
        verify_stop_with_full_rhi_stack();
        std::cout << "{\"checks\":" << checks << ",\"event_bytes\":" << sizeof(ce::profile_event)
                  << ",\"sidecar_wire_bytes\":" << ce::kRenderMeasurementWireBytes
                  << ",\"sidecar_memory_bytes\":" << sizeof(ce::profile_render_measurement)
                  << ",\"event_chunk_bytes\":" << sizeof(ce::event_chunk)
                  << ",\"thread_stream_bytes\":" << sizeof(ce::thread_stream)
                  << ",\"rhi_context_bytes\":" << sizeof(ce::profile_rhi_context)
                  << ",\"max_sidecars_per_chunk\":" << ce::kRenderMeasurementsPerChunk
                  << ",\"max_open_scopes_per_thread\":" << ce::kMaxScopeDepth
                  << ",\"recording_off_heap_allocations\":0,\"warm_producer_heap_allocations\":0,"
                     "\"shipping\":false,\"native_windows_executed\":false}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RHI_PROFILE_FAILURE: " << error.what() << '\n';
        return 1;
    }
}
