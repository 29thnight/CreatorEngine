#ifndef PRSM_TEST_TASK_BRIDGE_H
#define PRSM_TEST_TASK_BRIDGE_H

#include "prsm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draft mock-host bridge, NOT the stable engine ABI. Handles are opaque,
 * scheduler-specific and kind-checked; do not persist them beyond that host.
 * Every function requires a current scheduler callback on the game thread.
 * Failure returns PRSM_CALL_FAILED and resets its output; no exceptions cross C.
 * Generated code must propagate failures, normally as PRSM_TASK_PANICKED. */
typedef uint64_t PrsmHostTask;
typedef uint64_t PrsmHostGroup;

typedef enum PrsmHostGroupKind {
    PRSM_HOST_GATHER = 0,
    PRSM_HOST_FIRST = 1
} PrsmHostGroupKind;

CE_API PrsmCallStatus prsm_host_current_task(PrsmHostTask* out_task);

/* Resume-only: prepare the callback's outgoing wait, then return SUSPENDED.
 * Zero yields one game poll; n > 0 yields n subsequent game polls. Paused
 * engine frames must not call Poll. Failure resets the entire wait to zero. */
CE_API PrsmCallStatus prsm_host_wait_frames(uint32_t frames, PrsmWait* out_wait);

/* Creation is allowed only during resume, never cancel or predicate evaluation.
 * Frames must be live GC objects. Children inherit the current task's owner.
 * Keep child frames in traced parent slots if their results are needed after
 * completion: scheduler roots end when the child becomes terminal. */
CE_API PrsmCallStatus prsm_host_task_start(void* frame, PrsmHostTask* out_task);

/* Only direct children of the current task may be queried. SUSPENDED means
 * still live, including joining descendants or cooperative cancellation. */
CE_API PrsmCallStatus prsm_host_task_status(PrsmHostTask task, PrsmTaskStatus* out_status);

CE_API PrsmCallStatus prsm_host_group_create(uint32_t kind, PrsmHostGroup* out_group);
CE_API PrsmCallStatus prsm_host_group_add(PrsmHostGroup group, void* frame, PrsmHostTask* out_task);
CE_API PrsmCallStatus prsm_host_group_seal(PrsmHostGroup group);

/* Only groups created by the current task may be queried. Ready includes all
 * loser cancellation cleanup. Winner requires a ready FIRST group; otherwise
 * it fails. Compare the returned task handle with handles returned by add. */
CE_API PrsmCallStatus prsm_host_group_ready(PrsmHostGroup group, int* out_ready);
CE_API PrsmCallStatus prsm_host_group_winner(PrsmHostGroup group, PrsmHostTask* out_task);

/* Propagate recoverable failure from the current direct group branch. Resume
 * only; callback must return DONE after marking. Expected values returned as
 * ordinary data must not mark failure. Panic remains owner-fatal and distinct.
 * Gather selects earliest completion batch, source order breaks ties, then
 * cancels siblings and joins cleanup. First uses the selected winner's result.
 * Query requires a ready owned group: OK + zero means no selected error. */
CE_API PrsmCallStatus prsm_host_branch_fail(void);
CE_API PrsmCallStatus prsm_host_group_error(PrsmHostGroup group, PrsmHostTask* out_task);

#ifdef __cplusplus
}
#endif
#endif
