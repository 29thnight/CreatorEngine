#pragma once

#include <cstddef>
#include <cstdint>

namespace ce
{
    // These are measured axes, never an inferred display/FG cadence. A CPU
    // engine frame may own zero or many render submissions and GPU pass samples.
    enum class profile_render_axis : std::uint8_t
    {
        cpu_render_submit = 1,
        gpu_pass = 2,
        cpu_presenter_return = 3,
    };

    // Point sampled by the presentation owner after its native/SDK wrapper
    // returns. Not an exact native-call endpoint, GPU completion, SDK input
    // consumption, OS displayed event, or physical photon timestamp. The clock
    // is the capture's QPC ticks_per_second (clock_domain == 1).
    struct profile_presenter_return
    {
        std::uint64_t observed_tick = 0, sequence = 0;
        std::uint64_t real_frame_id = 0, publication_frame_id = 0, view_id = 0, scene_epoch = 0;
        std::uint64_t request_generation = 0, presenter_generation = 0, fault_revision = 0;
        std::int64_t native_code = 0;
        std::uint32_t interpolated_frame_count = 0;
        std::uint8_t provider = 0, status = 1, fault_mode = 0;
        bool native_gate_active = false, identity_valid = false;
        std::uint8_t clock_domain = 1;

        bool valid() const
        {
            return observed_tick != 0 && sequence != 0 && provider <= 3 && status <= 13 &&
                fault_mode <= 8 && clock_domain == 1 &&
                (provider != 0 || interpolated_frame_count == 0) &&
                (!identity_valid || (real_frame_id != 0 && publication_frame_id != 0 &&
                    view_id != 0 && scene_epoch != 0));
        }
    };

    // Value-only boundary: no renderer dependency, process pointers or globals.
    // Kind: 0 unknown, 1 real, 2 generated. Resolution: 0 unknown, 1 native,
    // 2 reconstructed, 3 native fallback, 4 spatial-scaled. Providers: 0..3.
    struct profile_render_provenance
    {
        std::uint8_t frame_kind = 0;
        std::uint8_t resolution_state = 0;
        std::uint8_t upscaler = 0;
        std::uint8_t frame_generator = 0;
        std::uint64_t real_frame_id = 0;
        std::uint64_t view_id = 0;
        std::uint64_t scene_epoch = 0;
        std::uint64_t publication_frame_id = 0;
        std::uint32_t generated_ordinal = 0;
        std::uint32_t render_width = 0;
        std::uint32_t render_height = 0;
        std::uint32_t display_width = 0;
        std::uint32_t display_height = 0;
        bool native_gate_active = false;
        bool spatial_provenance_available = false;
        std::uint8_t spatial_mode = 0;
        bool deep_dvc_applied = false;

        bool valid() const
        {
            if (frame_kind > 2 || resolution_state > 4 || upscaler > 3 || frame_generator > 3 ||
                spatial_mode > 2)
            {
                return false;
            }
            if (frame_kind == 0)
            {
                return resolution_state == 0 && !spatial_provenance_available && !native_gate_active;
            }
            if (real_frame_id == 0 || publication_frame_id == 0 || render_width == 0 || render_height == 0 ||
                display_width == 0 || display_height == 0 || resolution_state == 0 ||
                (frame_kind == 1 && generated_ordinal != 0) ||
                (frame_kind == 2 && (generated_ordinal == 0 || frame_generator == 0)))
            {
                return false;
            }
            if (resolution_state == 2)
            {
                return upscaler != 0 && (!spatial_provenance_available || spatial_mode != 1);
            }
            if (resolution_state == 4)
            {
                return upscaler == 0 && spatial_provenance_available && spatial_mode == 1 &&
                    render_width <= display_width && render_height <= display_height;
            }
            return upscaler == 0 && render_width == display_width && render_height == display_height &&
                (!spatial_provenance_available || spatial_mode != 1);
        }

        bool native_quality_eligible() const
        {
            return valid() && frame_kind == 1 && resolution_state == 1 && native_gate_active &&
                spatial_provenance_available && spatial_mode == 0 && !deep_dvc_applied &&
                upscaler == 0 && frame_generator == 0;
        }
    };

    // Stable portable wire size: axes 1/2 use a 40-byte sample key + 60-byte
    // provenance. Axis 3 uses a distinct 95-byte payload after frame/axis and
    // is admitted only by snapshot v6 / stream v7 and newer readers.
    // The full submission/view/publication identities survive narrowing in the
    // legacy event key. A lookup must reject ambiguous keys, never pick latest.
    inline constexpr std::size_t kRenderMeasurementWireBytes = 100;
    struct profile_render_measurement
    {
        std::uint32_t engine_frame = 0;
        profile_render_axis axis = profile_render_axis::cpu_render_submit;
        std::uint8_t queue = 0;
        std::uint16_t event_view = 0;
        std::uint32_t event_submission = 0;
        std::uint32_t marker = 0;
        std::uint64_t submission_id = 0;
        std::uint64_t tick_begin = 0;
        std::uint64_t tick_end = 0;
        profile_render_provenance provenance;
        profile_presenter_return presenter;

        bool valid() const
        {
            if (axis == profile_render_axis::cpu_presenter_return)
            {
                return presenter.valid();
            }
            return (axis == profile_render_axis::cpu_render_submit || axis == profile_render_axis::gpu_pass) &&
                tick_end >= tick_begin && provenance.valid() &&
                (axis != profile_render_axis::gpu_pass ||
                    (marker != 0 && event_submission == static_cast<std::uint32_t>(submission_id) &&
                        event_view == static_cast<std::uint16_t>(provenance.view_id)));
        }
    };

    // Shared field order for snapshot and streaming codecs. Adapters provide
    // put/get integral values; booleans are explicitly validated one-byte flags.
    template<typename Writer>
    void encode_render_measurement(Writer& out, const profile_render_measurement& value)
    {
        out.put(value.engine_frame);
        out.put(static_cast<std::uint8_t>(value.axis));
        if (value.axis == profile_render_axis::cpu_presenter_return)
        {
            const auto& p = value.presenter;
            out.put(p.observed_tick); out.put(p.sequence);
            out.put(p.real_frame_id); out.put(p.publication_frame_id);
            out.put(p.view_id); out.put(p.scene_epoch);
            out.put(p.request_generation); out.put(p.presenter_generation);
            out.put(p.fault_revision); out.put(p.native_code);
            out.put(p.interpolated_frame_count);
            out.put(p.provider); out.put(p.status); out.put(p.fault_mode);
            out.put(static_cast<std::uint8_t>(p.native_gate_active));
            out.put(static_cast<std::uint8_t>(p.identity_valid)); out.put(p.clock_domain);
            out.put(std::uint32_t{0}); out.put(std::uint8_t{0});
            return;
        }
        out.put(value.queue);
        out.put(value.event_view);
        out.put(value.event_submission);
        out.put(value.marker);
        out.put(value.submission_id);
        out.put(value.tick_begin);
        out.put(value.tick_end);
        const auto& p = value.provenance;
        out.put(p.frame_kind);
        out.put(p.resolution_state);
        out.put(p.upscaler);
        out.put(p.frame_generator);
        out.put(p.real_frame_id);
        out.put(p.view_id);
        out.put(p.scene_epoch);
        out.put(p.publication_frame_id);
        out.put(p.generated_ordinal);
        out.put(p.render_width);
        out.put(p.render_height);
        out.put(p.display_width);
        out.put(p.display_height);
        out.put(static_cast<std::uint8_t>(p.native_gate_active));
        out.put(static_cast<std::uint8_t>(p.spatial_provenance_available));
        out.put(p.spatial_mode);
        out.put(static_cast<std::uint8_t>(p.deep_dvc_applied));
    }

    template<typename Reader>
    bool decode_render_measurement(Reader& in, profile_render_measurement& value, bool allow_presenter = true)
    {
        std::uint8_t axis = 0, native = 0, spatial = 0, deep = 0;
        if (!in.get(value.engine_frame) || !in.get(axis))
        {
            return false;
        }
        value.axis = static_cast<profile_render_axis>(axis);
        if (value.axis == profile_render_axis::cpu_presenter_return)
        {
            if (!allow_presenter)
            {
                return false;
            }
            auto& p = value.presenter;
            std::uint8_t identity = 0, reserved8 = 0;
            std::uint32_t reserved32 = 0;
            if (!in.get(p.observed_tick) || !in.get(p.sequence) ||
                !in.get(p.real_frame_id) || !in.get(p.publication_frame_id) ||
                !in.get(p.view_id) || !in.get(p.scene_epoch) ||
                !in.get(p.request_generation) || !in.get(p.presenter_generation) ||
                !in.get(p.fault_revision) || !in.get(p.native_code) ||
                !in.get(p.interpolated_frame_count) || !in.get(p.provider) || !in.get(p.status) ||
                !in.get(p.fault_mode) || !in.get(native) || !in.get(identity) ||
                !in.get(p.clock_domain) || !in.get(reserved32) || !in.get(reserved8) ||
                native > 1 || identity > 1 || reserved32 != 0 || reserved8 != 0)
            {
                return false;
            }
            p.native_gate_active = native != 0;
            p.identity_valid = identity != 0;
            return value.valid();
        }
        auto& p = value.provenance;
        if (!in.get(value.queue) ||
            !in.get(value.event_view) || !in.get(value.event_submission) || !in.get(value.marker) ||
            !in.get(value.submission_id) || !in.get(value.tick_begin) || !in.get(value.tick_end) ||
            !in.get(p.frame_kind) || !in.get(p.resolution_state) || !in.get(p.upscaler) ||
            !in.get(p.frame_generator) || !in.get(p.real_frame_id) || !in.get(p.view_id) ||
            !in.get(p.scene_epoch) || !in.get(p.publication_frame_id) || !in.get(p.generated_ordinal) ||
            !in.get(p.render_width) || !in.get(p.render_height) || !in.get(p.display_width) ||
            !in.get(p.display_height) || !in.get(native) || !in.get(spatial) ||
            !in.get(p.spatial_mode) || !in.get(deep) || native > 1 || spatial > 1 || deep > 1)
        {
            return false;
        }
        value.axis = static_cast<profile_render_axis>(axis);
        p.native_gate_active = native != 0;
        p.spatial_provenance_available = spatial != 0;
        p.deep_dvc_applied = deep != 0;
        return value.valid();
    }
}
