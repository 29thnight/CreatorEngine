# Editor-owned viewer lifetime acceptance

Status: **UNEXECUTED**. This is a source-review and future Windows acceptance
checklist, not evidence of a build, test run, visual check or failure injection.
No production-only test switches, alternate launch paths or protocol messages
are added.

## Static ownership checks

- Every Editor viewer launch uses the pinned ordinary-token executable and the
  existing parent PID/creation time/session/nonce checks
- An unnamed job is created with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` before
  launch. Its handle is non-inheritable and `CreateProcessW` inherits no handles
- The job is assigned in `PROC_THREAD_ATTRIBUTE_JOB_LIST`, not by a racy
  post-launch assignment. The child starts suspended; failed identity or owner
  shutdown checks never resume it
- Job setup/assignment/resume failures fail closed; there is no unowned fallback,
  elevation, breakaway, image-name process scan or PID-based termination
- Transport failure leaves the owned process tracked until actual process exit
  or Editor shutdown. It cannot launch a duplicate viewer just because IPC failed
- Editor shutdown signals transport cancellation, gives `WM_CLOSE` a 1.5-second
  grace period, and uses only its private job for forced cleanup. Owner waiting
  is limited to 2.5 seconds even if its transport worker cannot finish
- The public standalone `ProfilerViewer.exe` and `--open` paths do not create or
  join an Editor lifetime job; loading a file in an already-owned viewer does not
  remove that ownership

## Future Windows runtime matrix

1. Launch viewers from two ordinary-token Editors, plus separate no-argument,
   `.ceprof` and `.cedx` viewers. Close one Editor normally. Only its own viewer
   exits; the other Editor and all independently launched viewers keep running
2. Repeat with the owned viewer focused, minimized, occluded, on each live tab,
   and displaying an offline file. Normal teardown saves settings when it
   finishes within the grace period; closing the viewer alone preserves engine
   recording and permits a fresh Trace launch
3. Terminate/crash one Editor after viewer startup, during process creation,
   before resume and during handshake. The job must leave no orphan or suspended
   viewer, and must not affect another Editor or standalone viewer
4. Disconnect/break the pipe while the Editor remains alive. Last immutable data
   remains visible and live actions are disabled. Repeated Trace focuses the
   same owned viewer. Subsequent Editor close/crash still closes it
5. Close the Editor while a large file is opening/indexing, a range is preparing,
   export is pending, native UI is stuck, or a mocked worker is blocked. Verify
   normal cancellation is attempted, only owned processes receive forced cleanup,
   and the Editor does not wait indefinitely for the viewer/transport. Do not
   treat a forced close as successful export or completed settings persistence
6. Race Trace/open with shutdown and repeat open/close/relaunch. Include shutdown
   before worker startup, during job setup, after process creation, immediately
   before/after resume, and while completing the prior viewer. No new child may
   escape ownership; a stop observed before resume leaves it suspended until
   owned cleanup
7. Force job creation/configuration, attribute allocation/update, process launch,
   identity validation (including allocation exceptions after suspended creation)
   and ResumeThread failures. Show the launch error without an unowned viewer,
   leaked suspended child, inherited job handle, or UAC prompt. Also force worker
   thread-creation failure while another Trace request arrives; failure cleanup
   must not overwrite the next launch's status or completion event
8. Run the Editor in a compatible enclosing job and a restrictive incompatible
   enclosing job. Verify nested ownership succeeds when allowed; otherwise launch
   fails closed rather than using breakaway or dropping the ownership attribute
9. Stress short-lived unrelated processes/PID reuse around close and focus.
   Handle-based lifetime checks and the exact private job must never address or
   terminate an unrelated process

The existing ETW collector/helper shutdown and crash-orphan acceptance are
separate. Passing this viewer matrix would not establish ETW session cleanup.

## Native API references

- [Process creation job-list attribute](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
- [Kill-on-close job limits](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information)
- [Job-scoped forced termination](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-terminatejobobject)
