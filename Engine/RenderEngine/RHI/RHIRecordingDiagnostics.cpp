#include "RHIRecordingDiagnostics.h"

const RHIRecordingPolicy& RHIRecordingPolicy::Get()
{
    static const RHIRecordingPolicy policy = []
    {
        RHIRecordingPolicy value;
        value.batchSubmit = ReadBounded("CREATOR_RECORDING_BATCH_SUBMIT", 1, 1) != 0;
        value.timingEnabled = ReadBounded("CREATOR_RECORDING_JOB_TRACE", 0, 1) != 0;
        return value;
    }();
    return policy;
}
