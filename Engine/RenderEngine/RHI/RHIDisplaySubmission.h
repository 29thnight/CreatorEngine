#pragma once

#include "RHISubmissionThread.h"

enum class RHIDisplaySubmissionStatus : uint8_t
{
    Pending,
    Succeeded,
    Failed,
};

/// CPU proof for publishing a rendered image. The frame tail may signal a
/// higher GPU fence even when an earlier admitted batch failed to translate.
/// Both submissions must therefore succeed independently of fence progress.
/// Callers still need a finite completed GPU point before exposing pixels.
/// An owned graph whose CPU submission already succeeded may pass its tail
/// ticket as both arguments. Missing tickets never establish publication.
inline RHIDisplaySubmissionStatus PollRHIDisplaySubmission(const RHISubmissionTicket& batch,
                                                           const RHISubmissionTicket& tail)
{
    if (!batch.IsValid() || !tail.IsValid())
    {
        return RHIDisplaySubmissionStatus::Failed;
    }

    const bool batchComplete = batch.IsComplete();
    const bool tailComplete = tail.IsComplete();
    RHICommandFailure failure;
    if ((batchComplete && batch.GetCommandFailure(failure)) || (tailComplete && tail.GetCommandFailure(failure)))
    {
        return RHIDisplaySubmissionStatus::Failed;
    }
    return batchComplete && tailComplete ? RHIDisplaySubmissionStatus::Succeeded : RHIDisplaySubmissionStatus::Pending;
}
