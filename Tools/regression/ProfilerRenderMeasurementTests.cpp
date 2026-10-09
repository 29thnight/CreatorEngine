// Source-only fixtures. Authored, NOT built or executed in this change.
// Link the standalone EngineDiagnostics profiler sources when execution is authorized.
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>

namespace profiler_render_measurement_tests
{
    struct wire
    {
        std::vector<std::byte> bytes;
        template<typename T>
        void put(T value)
        {
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xff));
            }
        }
        void text(const std::string& value)
        {
            put(static_cast<std::uint32_t>(value.size()));
            for (char character : value)
            {
                put(static_cast<std::uint8_t>(character));
            }
        }
        void append(const std::vector<std::byte>& value)
        {
            bytes.insert(bytes.end(), value.begin(), value.end());
        }
    };

    struct reader
    {
        std::span<const std::byte> bytes;
        std::size_t at = 0;
        template<typename T>
        bool get(T& value)
        {
            if (sizeof(T) > bytes.size() - at)
            {
                return false;
            }
            std::make_unsigned_t<T> bits = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bits |= static_cast<std::make_unsigned_t<T>>(std::to_integer<unsigned>(bytes[at++])) << (index * 8);
            }
            std::memcpy(&value, &bits, sizeof(value));
            return true;
        }
    };

    std::uint32_t crc(std::span<const std::byte> bytes)
    {
        std::uint32_t value = 0xffffffffu;
        for (std::byte byte : bytes)
        {
            value ^= std::to_integer<std::uint8_t>(byte);
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                value = (value & 1) != 0 ? 0xedb88320u ^ (value >> 1) : value >> 1;
            }
        }
        return value ^ 0xffffffffu;
    }

    void header(wire& output, std::uint32_t version, std::uint32_t chunks)
    {
        for (char character : std::array<char, 8>{ 'C', 'E', 'P', 'R', 'O', 'F', 0, 0 })
        {
            output.put(static_cast<std::uint8_t>(character));
        }
        output.put(version);
        output.put(chunks);
    }

    ce::profile_render_measurement sample(std::uint32_t frame, ce::profile_render_axis axis,
                                         std::uint64_t view = 0x10003, std::uint64_t submission = 0x100000005)
    {
        ce::profile_render_measurement result;
        result.engine_frame = frame;
        result.axis = axis;
        result.event_view = static_cast<std::uint16_t>(view);
        result.event_submission = static_cast<std::uint32_t>(submission);
        result.marker = axis == ce::profile_render_axis::gpu_pass ? 1 : 0;
        result.submission_id = submission;
        result.tick_begin = frame * 1000ull + 250;
        result.tick_end = frame * 1000ull + 750;
        auto& p = result.provenance;
        p.frame_kind = 1;
        p.resolution_state = 1;
        p.real_frame_id = 100000ull + frame;
        p.view_id = view;
        p.scene_epoch = 31;
        p.publication_frame_id = 200000ull + frame;
        p.render_width = p.display_width = 1920;
        p.render_height = p.display_height = 1080;
        p.native_gate_active = true;
        p.spatial_provenance_available = true;
        assert(result.valid());
        return result;
    }

    ce::profile_event event(const ce::profile_render_measurement& sample)
    {
        ce::profile_event value;
        value.frame = sample.engine_frame;
        value.marker = sample.marker;
        value.flags = ce::event_flags::gpu_span;
        value.tick_begin = sample.tick_begin;
        value.tick_end = sample.tick_end;
        value.submission = sample.event_submission;
        value.view = sample.event_view;
        value.queue = sample.queue;
        return value;
    }

    ce::profile_render_measurement presenter(std::uint32_t containerFrame, std::uint64_t sequence)
    {
        ce::profile_render_measurement value;
        value.engine_frame = containerFrame;
        value.axis = ce::profile_render_axis::cpu_presenter_return;
        auto& p = value.presenter;
        p.observed_tick = containerFrame * 1000ull + 900;
        p.sequence = sequence;
        p.real_frame_id = 0x100000001ull + sequence;
        p.publication_frame_id = 0x200000001ull + sequence;
        p.view_id = 0x300000001ull;
        p.scene_epoch = 0x400000001ull;
        p.request_generation = p.presenter_generation = 0x500000001ull;
        p.fault_revision = 0x600000001ull;
        p.native_code = -45008;
        p.provider = 2;
        p.interpolated_frame_count = 3;
        p.status = 10;
        p.fault_mode = 8;
        p.identity_valid = true;
        assert(value.valid());
        return value;
    }

    ce::capture_session_ptr capture(std::uint32_t count)
    {
        std::vector<ce::frame_record> frames;
        for (std::uint32_t number = 1; number <= count; ++number)
        {
            ce::frame_record frame;
            frame.engine_frame = number;
            frame.tick_begin = number * 1000ull;
            frame.tick_end = frame.tick_begin + 1000;
            frame.render_measurements.push_back(sample(number, ce::profile_render_axis::cpu_render_submit));
            const auto gpu = sample(number, ce::profile_render_axis::gpu_pass);
            frame.render_measurements.push_back(gpu);
            frame.render_measurements.push_back(presenter(number, number));
            frame.events.push_back(event(gpu));
            frames.push_back(std::move(frame));
        }
        return std::make_shared<const ce::capture_session>(std::move(frames),
            std::vector<ce::thread_info>{ { "GPU", 0, 0, ce::track_kind::gpu_graphics, 0 } },
            std::vector<ce::capture_marker>{ {}, { "GPU.Pass", "", 0, ce::marker_kind::gpu_span } },
            ce::capture_environment{ 1000000 }, true, 0);
    }

    void same(const ce::profile_render_measurement& left, const ce::profile_render_measurement& right)
    {
        wire a, b;
        ce::encode_render_measurement(a, left);
        ce::encode_render_measurement(b, right);
        assert(a.bytes.size() == ce::kRenderMeasurementWireBytes && a.bytes == b.bytes);
    }

    // Independently authored old layouts, not a new encoder with the header relabeled.
    void old_event(wire& output, std::uint32_t version)
    {
        output.put(std::uint64_t{ 1250 }); output.put(std::uint64_t{ 1750 });
        output.put(std::uint32_t{ 1 }); output.put(std::uint32_t{ 1 });
        output.put(std::uint16_t{ 0 }); output.put(std::uint16_t{ 0 });
        output.put(std::uint8_t{ 4 }); output.put(std::uint8_t{ 0 });
        output.put(std::uint32_t{ 5 }); output.put(std::uint16_t{ 3 }); output.put(std::uint16_t{ 0 });
        if (version >= 2)
        {
            output.put(std::uint64_t{ 0 }); output.put(std::uint64_t{ 0 }); output.put(std::uint64_t{ 0 });
        }
    }

    std::vector<std::byte> old_snapshot(std::uint32_t version, bool withFrame)
    {
        std::array<wire, 6> chunks;
        chunks[0].put(std::uint64_t{ 1000000 }); chunks[0].put(std::uint8_t{ 1 }); chunks[0].put(std::uint32_t{ 0 });
        chunks[1].put(std::uint32_t{ 2 });
        for (unsigned marker = 0; marker < 2; ++marker)
        {
            chunks[1].put(static_cast<std::uint8_t>(marker == 0 ? 0 : 3));
            chunks[1].put(std::uint32_t{ 0 }); chunks[1].text(marker == 0 ? "" : "GPU.Pass"); chunks[1].text("");
        }
        chunks[2].put(std::uint32_t{ 1 }); chunks[2].put(std::uint32_t{ 0 }); chunks[2].put(std::uint32_t{ 0 });
        chunks[2].put(std::uint8_t{ 4 }); chunks[2].put(std::uint32_t{ 0 }); chunks[2].text("GPU");
        chunks[3].put(static_cast<std::uint32_t>(withFrame));
        if (withFrame)
        {
            chunks[3].put(std::uint32_t{ 1 }); chunks[3].put(std::uint64_t{ 1000 }); chunks[3].put(std::uint64_t{ 2000 });
            chunks[3].put(std::uint64_t{ 0 }); chunks[3].put(std::uint32_t{ 1 }); old_event(chunks[3], version);
        }
        chunks[4].put(std::uint64_t{ 0 }); chunks[4].put(static_cast<std::uint32_t>(withFrame));
        if (withFrame)
        {
            chunks[4].put(std::uint32_t{ 1 }); chunks[4].put(std::uint32_t{ 0 });
        }
        chunks[5].put(std::uint32_t{ 0 });
        wire output;
        header(output, version, 6);
        std::uint64_t offset = 16 + 6 * 32;
        for (std::uint32_t index = 0; index < chunks.size(); ++index)
        {
            output.put(index + 1);
            output.put((index == 2 || index == 3 || index == 4) ? version : std::uint32_t{ 1 });
            output.put(offset); output.put(static_cast<std::uint64_t>(chunks[index].bytes.size()));
            output.put(crc(chunks[index].bytes)); output.put(std::uint32_t{ 0 });
            offset += chunks[index].bytes.size();
        }
        for (const auto& chunk : chunks)
        {
            output.append(chunk.bytes);
        }
        return output.bytes;
    }

    void record(wire& output, std::uint32_t type, std::uint64_t sequence, const std::vector<std::byte>& payload)
    {
        wire envelope;
        envelope.put(std::uint32_t{ 0x4b435043 }); envelope.put(type); envelope.put(sequence);
        envelope.put(static_cast<std::uint64_t>(payload.size())); envelope.put(crc(payload));
        envelope.put(crc(envelope.bytes)); output.append(envelope.bytes); output.append(payload);
    }

    std::vector<std::byte> old_stream()
    {
        wire output, frame, footer;
        header(output, 3, 0);
        record(output, 1, 0, old_snapshot(2, false));
        frame.put(std::uint32_t{ 1 }); frame.put(std::uint64_t{ 1000 }); frame.put(std::uint64_t{ 2000 });
        frame.put(std::uint64_t{ 0 }); frame.put(std::uint32_t{ 1 }); frame.put(std::uint32_t{ 0 }); old_event(frame, 2);
        record(output, 2, 1, frame.bytes);
        footer.put(std::uint8_t{ 1 }); footer.put(std::uint32_t{ 0 });
        for (std::uint64_t field : std::array<std::uint64_t, 12>{ 1, 1, 1000, 2000, 0, 0, 0, 0, 0, 0, 0, 0 })
        {
            footer.put(field);
        }
        assert(footer.bytes.size() == 101);
        record(output, 3, 2, footer.bytes);
        return output.bytes;
    }

    void write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assert(output.good());
    }

    void codec_and_legacy()
    {
        const auto original = capture(3);
        const auto encoded = ce::encode_capture_bounded(*original, 1024 * 1024);
        assert(encoded);
        const auto decoded = ce::decode_capture_bounded(*encoded, 1024 * 1024);
        assert(decoded && (*decoded)->frame_count() == 3);
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto& frame = (*decoded)->frames()[index];
            assert(frame.render_measurements.size() == 3);
            same(original->frames()[index].render_measurements[0], frame.render_measurements[0]);
            same(original->frames()[index].render_measurements[1], frame.render_measurements[1]);
            same(original->frames()[index].render_measurements[2], frame.render_measurements[2]);
            assert(frame.render_measurement_for(frame.events[0]) != nullptr);
        }
        assert(!ce::encode_capture_bounded(*original, encoded->size() - 1));
        for (std::uint32_t version : { 1u, 2u })
        {
            const auto legacy = ce::decode_capture(old_snapshot(version, true));
            assert(legacy && (*legacy)->frames()[0].render_measurements.empty());
            const auto resaved = ce::decode_capture(ce::encode_capture(**legacy));
            assert(resaved && (*resaved)->frames()[0].render_measurements.empty());
        }
        auto value = sample(1, ce::profile_render_axis::gpu_pass);
        wire bytes;
        ce::encode_render_measurement(bytes, value);
        for (std::size_t cut = 0; cut < bytes.bytes.size(); ++cut)
        {
            reader input{ std::span(bytes.bytes).first(cut) };
            ce::profile_render_measurement rejected;
            assert(!ce::decode_render_measurement(input, rejected));
        }
        bytes.bytes[97] = std::byte{ 2 }; // spatial availability is an exact Boolean.
        reader invalid{ bytes.bytes };
        assert(!ce::decode_render_measurement(invalid, value));
        value = sample(1, ce::profile_render_axis::gpu_pass);
        value.provenance.frame_kind = 2;
        value.provenance.frame_generator = 1;
        value.provenance.generated_ordinal = 1;
        assert(value.valid() && !value.provenance.native_quality_eligible());
        value.provenance.spatial_provenance_available = false;
        assert(!value.provenance.native_quality_eligible());
        // Fault-contaminated producer rows intentionally use the existing
        // unknown vocabulary; re-encoding cannot restore native eligibility.
        value = sample(1, ce::profile_render_axis::cpu_render_submit);
        value.provenance.frame_kind = 0;
        value.provenance.resolution_state = 0;
        value.provenance.native_gate_active = false;
        value.provenance.spatial_provenance_available = false;
        wire unknownBytes;
        ce::encode_render_measurement(unknownBytes, value);
        reader unknownInput{ unknownBytes.bytes };
        ce::profile_render_measurement unknown;
        assert(ce::decode_render_measurement(unknownInput, unknown));
        same(value, unknown);
        assert(unknown.provenance.frame_kind == 0 && !unknown.provenance.native_quality_eligible());
        wire presenterBytes;
        const auto observed = presenter(9, 100);
        ce::encode_render_measurement(presenterBytes, observed);
        assert(presenterBytes.bytes.size() == 100);
        reader presenterInput{ presenterBytes.bytes };
        ce::profile_render_measurement reopened;
        assert(ce::decode_render_measurement(presenterInput, reopened, true));
        same(observed, reopened);
        reader legacyPresenterInput{ presenterBytes.bytes };
        assert(!ce::decode_render_measurement(legacyPresenterInput, reopened, false));
        for (std::size_t cut = 0; cut < presenterBytes.bytes.size(); ++cut)
        {
            reader truncated{ std::span(presenterBytes.bytes).first(cut) };
            assert(!ce::decode_render_measurement(truncated, reopened, true));
        }
        presenterBytes.bytes.back() = std::byte{1};
        reader badReserved{ presenterBytes.bytes };
        assert(!ce::decode_render_measurement(badReserved, reopened, true));
    }

    void streaming_and_selected_windows()
    {
        const auto directory = std::filesystem::temp_directory_path() /
            ("ceprof-render-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(std::filesystem::create_directory(directory));
        struct cleanup
        {
            std::filesystem::path directory;
            ~cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(directory, ignored);
            }
        } files{ directory };
        const auto original = capture(705);
        const auto streamPath = directory / "continuous7.ceprof";
        auto writer = ce::continuous_capture_writer::start(streamPath);
        assert(writer && (*writer)->append_final(original));
        (*writer)->request_finalize(true, 0);
        (*writer)->wait();
        assert((*writer)->status().state == ce::recording_state::finalized);
        writer->reset();
        const auto streaming = ce::open_capture_recording(streamPath);
        assert(streaming && (*streaming)->frame_count() == 705 && (*streaming)->complete());
        const auto tail = ce::load_capture(streamPath);
        assert(tail && (*tail)->frame_count() == 600 && (*tail)->frames().front().engine_frame == 106);
        const auto first = (*streaming)->load_range(0, 1);
        assert(first && (*first)->frames()[0].engine_frame == 1);
        same(original->frames()[0].render_measurements[1], (*first)->frames()[0].render_measurements[1]);
        same(original->frames()[0].render_measurements[2], (*first)->frames()[0].render_measurements[2]);
        same(original->frames()[704].render_measurements[2], (*tail)->frames().back().render_measurements[2]);
        const auto snapshotPath = directory / "snapshot6.ceprof";
        write_file(snapshotPath, ce::encode_capture(*original));
        const auto snapshot = ce::open_capture_recording(snapshotPath);
        assert(snapshot && (*snapshot)->frame_count() == 705);
        const auto selection = (*snapshot)->load_range(601, 3);
        assert(selection && (*selection)->frames()[0].engine_frame == 602);
        same(original->frames()[601].render_measurements[1], (*selection)->frames()[0].render_measurements[1]);
        same(original->frames()[601].render_measurements[2], (*selection)->frames()[0].render_measurements[2]);
        assert(!(*snapshot)->load_range(601, 3, 1));
        const auto oldPath = directory / "continuous3.ceprof";
        write_file(oldPath, old_stream());
        const auto legacy = ce::load_capture(oldPath);
        assert(legacy && (*legacy)->frame_count() == 1 && (*legacy)->frames()[0].render_measurements.empty());
    }

    void producer_and_late_frame_ownership()
    {
        auto pool = std::make_shared<ce::chunk_pool>();
        pool->initialize(4, 4);
        ce::thread_stream stream(*pool, { "GPU", 0, 0, ce::track_kind::gpu_graphics, 0 });
        stream.set_generation(7);
        ce::capture_ring ring;
        ring.configure(2, ce::kDefaultMemoryBudget);
        auto value = sample(11, ce::profile_render_axis::gpu_pass);
        value.marker = ce::intern_runtime_marker("Submitted.Pass", ce::marker_kind::gpu_span);
        ce::gpu_span_context gpu;
        gpu.generation = 7;
        gpu.submission = value.event_submission;
        gpu.submission_id = value.submission_id;
        gpu.view = value.event_view;
        gpu.provenance = value.provenance;
        stream.write_span(value.marker, value.tick_begin, value.tick_end, 11, 0, gpu);
        stream.publish_frame();
        ring.ingest(pool->take_sealed(), pool, 7, 11000);
        ring.close_frame(11, 11000, 12000);
        ring.close_frame(12, 12000, 13000);
        const auto frozen = ring.freeze({}, { 1000000 }, true, 0);
        assert(frozen->find_frame(11)->render_measurements.size() == 1);
        assert(frozen->find_frame(12)->render_measurements.empty());
        same(value, frozen->find_frame(11)->render_measurements[0]);
        assert(frozen->find_frame(11)->memory_bytes() >= sizeof(ce::profile_render_measurement));
        auto duplicate = *frozen->find_frame(11);
        duplicate.render_measurements.push_back(value);
        assert(duplicate.render_measurement_for(duplicate.events[0]) == nullptr);
        // A newer sample cannot mutate the already frozen capture or relabel an
        // evicted owner frame. Its absence is explicitly counted as source loss.
        ring.close_frame(13, 13000, 14000);
        stream.write_render_measurement(value, 7);
        stream.publish_frame();
        ring.ingest(pool->take_sealed(), pool, 7, 14000);
        assert(ring.late_spans_dropped() != 0 && ring.dropped_events() != 0);
        same(value, frozen->find_frame(11)->render_measurements[0]);
    }

    void presenter_cpu_admission_and_bounded_loss()
    {
        auto pool = std::make_shared<ce::chunk_pool>();
        pool->initialize(1, 1);
        ce::thread_stream stream(*pool, { "Presenter", 0, 0, ce::track_kind::other, 0 });
        stream.set_generation(7);
        ce::capture_ring ring;
        ring.configure(2, ce::kDefaultMemoryBudget);
        const auto observed = presenter(41, 900);
        stream.write_presenter_return(observed, 7);
        stream.publish_frame();
        // The only bounded page is sealed, not silently replaced/expanded.
        stream.write_presenter_return(presenter(41, 901), 7);
        assert(stream.dropped_events() != 0);
        ring.ingest(pool->take_sealed(), pool, 7, 41000);
        ring.close_frame(41, 41000, 42000);
        auto frozen = ring.freeze({}, {1000000}, true, 0);
        assert(frozen->find_frame(41)->render_measurements.size() == 1);
        same(observed, frozen->find_frame(41)->render_measurements[0]);
        assert(observed.presenter.real_frame_id != observed.engine_frame);
        stream.request_freeze(42000);
        stream.write_presenter_return(presenter(42, 902), 7);
        stream.publish_frame();
        assert(pool->take_sealed() == nullptr); // No CPU admission after Stop.
    }

    void zero_frame_multiple_views_and_submissions()
    {
        ce::frame_record frame;
        frame.engine_frame = 0; // Zero is a real host boundary, not an unavailable sentinel.
        frame.tick_end = 1000;
        const auto first = sample(0, ce::profile_render_axis::gpu_pass, 7, 101);
        const auto second = sample(0, ce::profile_render_axis::gpu_pass, 8, 102);
        const auto repeated = sample(0, ce::profile_render_axis::gpu_pass, 7, 103);
        for (const auto& value : { first, second, repeated })
        {
            assert(value.provenance.publication_frame_id != value.engine_frame);
            assert(value.provenance.real_frame_id != value.engine_frame);
            frame.render_measurements.push_back(value);
            frame.events.push_back(event(value));
        }
        for (std::size_t index = 0; index < frame.events.size(); ++index)
        {
            const auto* found = frame.render_measurement_for(frame.events[index]);
            assert(found);
            same(frame.render_measurements[index], *found);
        }
        const ce::capture_session original({ frame },
            { { "GPU", 0, 0, ce::track_kind::gpu_graphics, 0 } },
            { {}, { "GPU.Pass", "", 0, ce::marker_kind::gpu_span } }, { 1000000 }, true, 0);
        const auto reopened = ce::decode_capture(ce::encode_capture(original));
        assert(reopened && (*reopened)->find_frame(0) && (*reopened)->find_frame(0)->render_measurements.size() == 3);
        same(repeated, (*reopened)->find_frame(0)->render_measurements[2]);

        // Full-width IDs may share legacy 16/32-bit event keys. Such a join
        // remains unavailable instead of attributing a pass to the latest view.
        auto collision = first;
        collision.provenance.view_id += 0x10000;
        collision.submission_id += 0x100000000ull;
        assert(collision.valid());
        frame.render_measurements.push_back(collision);
        assert(frame.render_measurement_for(frame.events[0]) == nullptr);
    }
}

int main()
{
    profiler_render_measurement_tests::codec_and_legacy();
    profiler_render_measurement_tests::streaming_and_selected_windows();
    profiler_render_measurement_tests::producer_and_late_frame_ownership();
    profiler_render_measurement_tests::zero_frame_multiple_views_and_submissions();
    profiler_render_measurement_tests::presenter_cpu_admission_and_bounded_loss();
}
