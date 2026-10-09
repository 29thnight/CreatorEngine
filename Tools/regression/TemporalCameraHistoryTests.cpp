// Authored source-only fixture. UNEXECUTED: no build, compiler or test was run.
// Link the production Camera implementation and its ordinary RenderEngine
// dependencies. This checks the producer/predicate contract, not SDK pixels.
#include "../../Engine/RenderEngine/Camera.h"
#include <cstdio>

namespace
{
    unsigned failures = 0;

    void Expect(bool value, const char* message)
    {
        if (!value)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }
}

int main()
{
    constexpr float aspect = 16.f / 9.f;
    Camera camera;
    auto committed = camera.CaptureFrameSnapshot(aspect);
    Expect(committed.HasSameTemporalHistory(committed), "unchanged camera preserves history");

    // There is deliberately no arbitrary teleport-distance heuristic. A normal
    // motion writer keeps history at any speed; a cut writer must notify.
    camera.m_eyePosition.x += 1000.f;
    auto candidate = camera.CaptureFrameSnapshot(aspect);
    candidate.editorCameraRevision = 100;
    candidate.editorInputSequence = 100;
    Expect(candidate.HasSameTemporalHistory(committed),
        "ordinary motion and editor diagnostic revisions do not reset history");
    camera.NotifyCameraCut();
    candidate = camera.CaptureFrameSnapshot(aspect);
    Expect(candidate.cameraCutRevision == committed.cameraCutRevision + 1,
        "explicit cut advances an independent revision in the sealed snapshot");
    Expect(!candidate.HasSameTemporalHistory(committed), "same-camera explicit cut resets history");
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "capturing or dropping an attempted frame cannot consume a cut");
    committed = candidate;
    Expect(camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "committed cut permits subsequent continuous frames");

    camera.MoveToTarget({ 2.f, 3.f, 4.f });
    candidate = camera.CaptureFrameSnapshot(aspect);
    Expect(!candidate.HasSameTemporalHistory(committed), "editor focus-to-target cuts history");
    committed = candidate;
    camera.MoveToTarget({ 2.f, 3.f, 4.f });
    Expect(camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "unchanged focus target does not repeatedly invalidate history");

    camera.m_fov = 45.f;
    candidate = camera.CaptureFrameSnapshot(aspect);
    Expect(!candidate.HasSameTemporalHistory(committed), "same-camera FOV change resets history");
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "an uncommitted projection change remains a pending reset");
    committed = candidate;
    camera.m_nearPlane = 0.2f;
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "near plane change resets history");
    committed = camera.CaptureFrameSnapshot(aspect);
    camera.m_farPlane = 1000.f;
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "far plane change resets history");
    committed = camera.CaptureFrameSnapshot(aspect);
    Expect(!camera.CaptureFrameSnapshot(4.f / 3.f).HasSameTemporalHistory(committed),
        "unjittered projection aspect change resets history");

    camera.m_isOrthographic = true;
    camera.m_viewWidth = 10.f;
    camera.m_viewHeight = 6.f;
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "projection kind change resets history");
    committed = camera.CaptureFrameSnapshot(aspect);
    camera.m_viewWidth = 20.f;
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "orthographic width change resets history");
    committed = camera.CaptureFrameSnapshot(aspect);
    camera.m_viewHeight = 12.f;
    Expect(!camera.CaptureFrameSnapshot(aspect).HasSameTemporalHistory(committed),
        "orthographic height change resets history");
    return failures ? 1 : 0;
}
