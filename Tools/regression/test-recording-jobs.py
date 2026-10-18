"""측정 실행 없이 기록 스냅샷의 중복·시계 경계·누락·누계 의미를 검증하는 fixture다."""

import copy
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "recording_jobs", Path(__file__).with_name("analyze-recording-jobs.py"))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def spin(frame=17, attempts=1, hits=1, reserved=25000, active=0):
    return dict(frameId=frame, scopeAccepted=True, attempts=attempts, taskHits=hits,
                budgetExhaustions=0, scopeExits=0, shutdownExits=0, wallNs=5000 * attempts,
                reservedBudgetNs=reserved, activeSpinners=active, maxActiveSpinners=1,
                axis="frame-cumulative-snapshot-inflight-exits-may-follow")


def fixture(offset=1000000000, frame=17):
    node0 = dict(node=0, target=0, wave=0, registeredNs=offset + 125, readyNs=offset + 160,
                 publishBeginNs=offset + 170, publishEndNs=offset + 300, startNs=offset + 190,
                 bodyEndNs=offset + 240, completeNs=offset + 260, publishEnkiThread=1, executeEnkiThread=2,
                 publishOsThread=100, executeOsThread=200, dispatcherRoute=True, skipped=False)
    node1 = dict(node=1, target=1, wave=1, registeredNs=offset + 130, readyNs=offset + 270,
                 publishBeginNs=offset + 280, publishEndNs=offset + 290, startNs=offset + 320,
                 bodyEndNs=offset + 360, completeNs=offset + 380, publishEnkiThread=2, executeEnkiThread=3,
                 publishOsThread=200, executeOsThread=300, dispatcherRoute=False, skipped=False)
    return dict(
        schemaVersion=1, clock=MODULE.CLOCK, axis=MODULE.AXIS, unitsAxis=MODULE.UNIT_AXIS,
        frameId=frame, timingEnabled=True,
        dependencyGraph=True, batchSubmit=True, unitTimingsTruncated=False, jobTimingsTruncated=False,
        dispatchBeginNs=offset + 100, waitBeginNs=offset + 180, returnNs=offset + 450,
        policy=dict(ownerSpinUs=25, workerSpinUs=25, frameSpinUs=100, spinWorkers=2),
        ownerWait=dict(available=True, spinElapsedNs=220, spinAttempted=True, completedDuringSpin=True,
                       blockingPathEntered=False, blockingAxis="cv-predicate-false-not-proven-os-sleep"),
        workerSpin=spin(frame),
        jobs=dict(available=True, submittedNs=offset + 120, dispatcherBeginNs=offset + 150,
                  completedNs=offset + 400, dispatcherHandoffs=1, nativeTasks=2, nodes=[node0, node1],
                  publicationAxis="enqueue-call-bracket-not-exact-queue-insertion"),
        units=[
            dict(name="Producer", **{"pass": 0}, target=0, wave=0, slice=0, sliceCount=1,
                 estimatedWork=2, beginNs=offset + 200, endNs=offset + 230, callbackNs=20, callbackInvocations=2),
            dict(name="Consumer", **{"pass": 1}, target=1, wave=1, slice=0, sliceCount=1,
                 estimatedWork=1, beginNs=offset + 330, endNs=offset + 350, callbackNs=15, callbackInvocations=1),
        ])


def fixture2(offset=1000000000, frame=17):
    trace = fixture(offset, frame)
    trace.update(schemaVersion=2, policy=dict(ownerWaitMode="blocking"))
    trace.pop("ownerWait")
    trace.pop("workerSpin")
    trace["jobs"].update(topologyAvailable=True, topologyTruncated=False, lastCompletedNodes=[1])
    trace["jobs"]["nodes"][0].update(dependencies=[], units=[0])
    trace["jobs"]["nodes"][1].update(dependencies=[dict(node=0, reasons=["cpu-producer"])], units=[1])
    for identity, unit in enumerate(trace["units"]):
        unit.update(unit=identity, node=identity)
    return trace


def without_topology(trace, truncated=False):
    trace["jobs"].update(topologyAvailable=False, topologyTruncated=truncated)
    for node in trace["jobs"].get("nodes", []):
        node.pop("dependencies", None)
        node.pop("units", None)
    for unit in trace["units"]:
        unit.pop("node", None)
    return trace


def fork_fixture2():
    trace = fixture2()
    trace["jobs"]["nodes"][1]["dependencies"] = []
    trace["jobs"]["nodes"][1]["dispatcherRoute"] = True
    offset = 1000000000
    last = copy.deepcopy(trace["jobs"]["nodes"][1])
    last.update(node=2, target=2, wave=2, registeredNs=offset + 135, readyNs=offset + 395,
                publishBeginNs=offset + 400, publishEndNs=offset + 420, startNs=offset + 425,
                bodyEndNs=offset + 440, completeNs=offset + 450, dispatcherRoute=False,
                dependencies=[dict(node=identity, reasons=["cpu-producer"]) for identity in (0, 1)], units=[2])
    trace["jobs"]["nodes"].append(last)
    trace["jobs"].update(nativeTasks=3, lastCompletedNodes=[2], completedNs=offset + 480)
    trace["returnNs"] = offset + 500
    trace["units"].append(dict(unit=2, node=2, name="Join", **{"pass": 2}, target=2, wave=2,
                               slice=0, sliceCount=1, estimatedWork=1,
                               beginNs=offset + 430, endNs=offset + 435, callbackNs=5, callbackInvocations=1))
    return trace


def wrapped(trace, view="2"):
    return dict(result=dict(data=dict(nativeRecord=dict(
        temporalProvenance=dict(viewId=view), schedulingTrace=trace))))


def analyze(*traces):
    return MODULE.analyze([("capture", wrapped(trace)) for trace in traces])


def cli_exit(trace):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "capture.json"
        path.write_text(json.dumps(wrapped(trace)), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            return MODULE.main([str(path), "--json"])


class RecordingJobsTests(unittest.TestCase):
    def test_native_intervals_and_publication_overlap(self):
        result = analyze(fixture())
        group = result["policies"][0]
        self.assertFalse(result["warnings"])
        self.assertTrue(group["coverageComplete"])
        self.assertEqual(group["metricsNs"]["dispatchToReturn"]["p50"], 350)
        self.assertEqual(group["metricsNs"]["graphCompleteToOwnerReturn"]["p50"], 50)
        self.assertEqual(group["metricsNs"]["registeredToReady"]["p50"], 87.5)
        self.assertEqual(group["metricsNs"]["readyToPublishBegin"]["p50"], 10)
        self.assertEqual(group["metricsNs"]["publishBeginToStart"]["p50"], 30)
        self.assertEqual(group["metricsNs"]["nodeBody"]["p50"], 45)
        self.assertEqual(group["metricsNs"]["bodyEndToComplete"]["p50"], 20)
        self.assertTrue(group["slowestNodes"][0]["executionOverlapsPublication"])
        self.assertEqual(group["slowestUnits"][0]["name"], "Producer")
        self.assertEqual(group["slowestUnits"][0]["bodyWallPerEstimatedWorkNs"], 15)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["p50"], 25)
        self.assertEqual(group["metricsNs"]["unitCallbackTotal"]["total"], 35)
        self.assertEqual(group["callbackInvocations"], 3)

    def test_repeated_command_snapshots_are_not_extra_samples(self):
        snapshot = wrapped(fixture())
        result = MODULE.analyze([("capture", [snapshot, {"response": copy.deepcopy(snapshot)}])])
        self.assertEqual(result["counts"]["foundTraces"], 2)
        self.assertEqual(result["counts"]["duplicateTraces"], 1)
        self.assertEqual(result["policies"][0]["metricsNs"]["nodeBody"]["count"], 2)
        self.assertEqual(len(result["workerSpinFrames"]), 1)
        self.assertEqual(result["workerSpinFrames"][0]["statistics"]["reservedBudgetNs"], 25000)

    def test_distinct_input_sessions_never_share_frame_identity(self):
        payload = wrapped(fixture())
        result = MODULE.analyze([("run-a.json", payload), ("run-b.json", payload)])
        self.assertEqual(result["counts"]["uniqueTraces"], 2)
        self.assertEqual(result["counts"]["duplicateTraces"], 0)
        self.assertEqual(len(result["policies"]), 2)
        self.assertEqual(len(result["workerSpinFrames"]), 2)

    def test_view_timing_separate_and_worker_frame_counters_shared(self):
        scene, game = fixture(), fixture(offset=2000000000)
        game["workerSpin"] = spin(attempts=2, hits=2, reserved=50000)
        result = MODULE.analyze([("capture", wrapped(scene, "1")), ("capture", wrapped(game, "2"))])
        self.assertEqual(len(result["policies"]), 2)
        self.assertEqual(len(result["workerSpinFrames"]), 1)
        frame = result["workerSpinFrames"][0]
        self.assertEqual(frame["statistics"]["reservedBudgetNs"], 50000)
        self.assertEqual(frame["statistics"]["attempts"], 2)
        self.assertFalse(frame["frameFinal"])

    def test_timing_disabled_does_not_create_zero_latency_samples(self):
        trace = fixture()
        trace.update(timingEnabled=False, dispatchBeginNs=0, waitBeginNs=0, returnNs=0, units=[],
                     jobs=dict(available=False, nodes=[]))
        result = analyze(trace)
        self.assertEqual(result["traces"][0]["status"], "timing-disabled")
        self.assertEqual(result["policies"][0]["metricsNs"], {})
        self.assertEqual(result["workerSpinFrames"][0]["statistics"]["attempts"], 1)

    def test_unsupported_schema_and_clock_are_rejected(self):
        schema = fixture()
        schema["schemaVersion"] = 3
        clock = fixture()
        clock["clock"] = "ETW-QPC"
        result = analyze(schema, clock)
        self.assertEqual(result["counts"]["invalidTraces"], 2)
        self.assertEqual(result["policies"], [])

    def test_noninteger_and_zero_timestamps_do_not_become_durations(self):
        trace = fixture()
        trace["jobs"]["nodes"][0]["startNs"] = 0
        trace["jobs"]["nodes"][1]["readyNs"] = True
        result = analyze(trace)
        group = result["policies"][0]
        self.assertEqual(group["invalidNodes"], 2)
        self.assertNotIn("nodeBody", group["metricsNs"])
        self.assertFalse(group["coverageComplete"])

    def test_reversed_causal_stage_is_excluded(self):
        trace = fixture()
        trace["jobs"]["nodes"][0]["readyNs"] = trace["jobs"]["nodes"][0]["registeredNs"] - 1
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidNodes"], 1)
        self.assertEqual(group["metricsNs"]["nodeBody"]["count"], 1)

    def test_skipped_scheduler_body_is_not_a_user_callback_sample(self):
        trace = fixture()
        trace["jobs"]["nodes"][1]["skipped"] = True
        trace["units"][1].update(beginNs=0, endNs=0, callbackNs=0, callbackInvocations=0)
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["skippedNodes"], 1)
        self.assertEqual(group["metricsNs"]["nodeBody"]["count"], 1)
        self.assertEqual(group["metricsNs"]["skippedNodeBody"]["count"], 1)
        self.assertTrue(all(not row["skipped"] for row in group["slowestNodes"]))

    def test_truncated_and_unavailable_jobs_preserve_partial_unit_data(self):
        trace = fixture()
        trace.update(jobTimingsTruncated=True, unitTimingsTruncated=True, jobs=dict(available=False, nodes=[]))
        result = analyze(trace)
        group = result["policies"][0]
        self.assertEqual(result["traces"][0]["jobsStatus"], "truncated-unavailable")
        self.assertEqual(group["jobTimingUnavailable"], 1)
        self.assertNotIn("nodeBody", group["metricsNs"])
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["count"], 2)
        self.assertFalse(group["coverageComplete"])

    def test_legacy_path_without_job_stages_is_unavailable_not_zero(self):
        trace = fixture()
        trace.update(dependencyGraph=False, batchSubmit=False, waitBeginNs=0,
                     jobs=dict(available=False, nodes=[]))
        trace["policy"]["ownerSpinUs"] = 0
        trace["ownerWait"].update(available=False, spinElapsedNs=0, spinAttempted=False,
                                  completedDuringSpin=False, blockingPathEntered=False)
        result = analyze(trace)
        group = result["policies"][0]
        self.assertEqual(result["counts"]["invalidTraces"], 0)
        self.assertNotIn("waitBeginToReturn", group["metricsNs"])
        self.assertNotIn("ownerSpinWall", group["metricsNs"])
        self.assertNotIn("submittedToDispatcher", group["metricsNs"])
        self.assertEqual(group["ownerWaitUnavailable"], 1)
        self.assertEqual(group["jobTimingUnavailable"], 1)

    def test_owner_wait_without_availability_flag_is_not_observed(self):
        trace = fixture()
        trace["ownerWait"].pop("available")
        group = analyze(trace)["policies"][0]
        self.assertNotIn("ownerSpinWall", group["metricsNs"])
        self.assertEqual(group["ownerWaitUnavailable"], 1)

    def test_owner_wait_availability_must_be_boolean(self):
        trace = fixture()
        trace["ownerWait"]["available"] = 1
        result = analyze(trace)
        self.assertEqual(result["policies"][0]["invalidOwnerWaits"], 1)
        self.assertEqual(result["counts"]["invalidTraces"], 1)

    def test_unobserved_unit_is_not_a_fast_zero_sample(self):
        trace = fixture()
        trace["units"][0].update(beginNs=0, endNs=0)
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["unavailableUnits"], 1)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["count"], 1)

    def test_missing_callback_aggregates_do_not_relabel_record_body(self):
        trace = fixture()
        for unit in trace["units"]:
            unit.pop("callbackNs")
            unit.pop("callbackInvocations")
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["callbackTimingUnavailableUnits"], 2)
        self.assertIn("unitRecordBody", group["metricsNs"])
        self.assertNotIn("unitCallbackTotal", group["metricsNs"])

    def test_callback_sum_cannot_exceed_containing_record_body(self):
        trace = fixture()
        trace["units"][0]["callbackNs"] = 100
        result = analyze(trace)
        group = result["policies"][0]
        self.assertEqual(group["callbackTimingUnavailableUnits"], 1)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["count"], 2)
        self.assertEqual(group["metricsNs"]["unitCallbackTotal"]["count"], 1)

    def test_unit_body_cannot_outlive_its_native_node(self):
        trace = fixture()
        trace["units"][0]["endNs"] = trace["jobs"]["nodes"][0]["bodyEndNs"] + 1
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidUnits"], 1)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["count"], 1)

    def test_skipped_node_cannot_have_an_observed_unit_body(self):
        trace = fixture()
        trace["jobs"]["nodes"][0]["skipped"] = True
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidUnits"], 1)

    def test_active_worker_snapshot_is_not_final_and_counters_are_not_atomic(self):
        trace = fixture()
        trace["workerSpin"] = spin(attempts=1, hits=2, active=1)
        result = analyze(trace)
        frame = result["workerSpinFrames"][0]
        self.assertFalse(result["warnings"])
        self.assertTrue(frame["snapshotInFlight"])
        self.assertTrue(frame["countersAreNonTransactional"])
        self.assertFalse(frame["frameFinal"])

    def test_spin_wall_can_exceed_reserved_budget_under_preemption(self):
        trace = fixture()
        trace["workerSpin"]["wallNs"] = 10000000
        result = analyze(trace)
        self.assertEqual(result["counts"]["invalidSpinSnapshots"], 0)
        self.assertEqual(result["workerSpinFrames"][0]["statistics"]["wallNs"], 10000000)

    def test_rejected_scope_is_unavailable_instead_of_zero_frame(self):
        trace = fixture()
        trace["workerSpin"] = dict(scopeAccepted=False, frameId=0)
        result = analyze(trace)
        self.assertEqual(result["counts"]["unacceptedSpinScopeSnapshots"], 1)
        self.assertEqual(result["workerSpinFrames"], [])

    def test_counter_regression_within_one_frame_is_ambiguous(self):
        earlier, later = fixture(), fixture(offset=2000000000)
        earlier["workerSpin"] = spin(attempts=2, hits=2, reserved=50000)
        later["workerSpin"] = spin(attempts=1, hits=1, reserved=25000)
        result = analyze(earlier, later)
        self.assertEqual(result["workerSpinFrames"][0]["status"], "ambiguous-cumulative-snapshots")
        self.assertIsNone(result["workerSpinFrames"][0]["statistics"])

    def test_conflicting_duplicate_is_not_chosen_for_a_faster_result(self):
        first = fixture()
        second = copy.deepcopy(first)
        second["jobs"]["nodes"][0]["bodyEndNs"] -= 5
        result = analyze(first, second)
        self.assertEqual(result["counts"]["conflictingTraces"], 1)
        self.assertEqual(result["counts"]["invalidTraces"], 1)
        self.assertTrue(all(not group["metricsNs"] for group in result["policies"]))
        self.assertFalse(result["policies"][0]["coverageComplete"])
        self.assertEqual(result["workerSpinFrames"], [])

    def test_incomplete_or_reversed_recording_interval_is_rejected(self):
        zero, reversed_trace = fixture(), fixture(offset=2000000000)
        zero["returnNs"] = 0
        reversed_trace["returnNs"] = reversed_trace["dispatchBeginNs"] - 1
        result = analyze(zero, reversed_trace)
        self.assertEqual(result["counts"]["invalidTraces"], 2)
        self.assertTrue(all(not group["metricsNs"] for group in result["policies"]))
        self.assertEqual(result["workerSpinFrames"], [])

    def test_duplicate_node_ids_exclude_every_affected_row(self):
        trace = fixture()
        duplicate = copy.deepcopy(trace["jobs"]["nodes"][0])
        duplicate["bodyEndNs"] -= 1
        trace["jobs"]["nodes"].append(duplicate)
        trace["jobs"]["nativeTasks"] = 3
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidNodes"], 2)
        self.assertEqual(group["metricsNs"]["nodeBody"]["count"], 1)
        self.assertEqual(group["metricsNs"]["nodeBody"]["p50"], 40)

    def test_duplicate_unit_ids_exclude_every_affected_row(self):
        trace = fixture()
        duplicate = copy.deepcopy(trace["units"][0])
        duplicate["endNs"] -= 1
        trace["units"].append(duplicate)
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidUnits"], 2)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["count"], 1)
        self.assertEqual(group["metricsNs"]["unitRecordBody"]["p50"], 20)

    def test_only_one_unit_endpoint_is_invalid_not_unobserved(self):
        trace = fixture()
        trace["units"][0]["beginNs"] = 0
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidUnits"], 1)
        self.assertEqual(group["unavailableUnits"], 0)

    def test_owner_spin_wall_must_fit_the_measured_wait_interval(self):
        trace = fixture()
        trace["ownerWait"]["spinElapsedNs"] = trace["returnNs"] - trace["waitBeginNs"] + 1
        result = analyze(trace)
        self.assertEqual(result["policies"][0]["invalidOwnerWaits"], 1)
        self.assertFalse(result["policies"][0]["coverageComplete"])
        self.assertEqual(result["workerSpinFrames"], [])

    def test_valid_and_invalid_interval_in_one_policy_has_incomplete_coverage(self):
        valid, invalid = fixture(), fixture(offset=2000000000)
        invalid["returnNs"] = 0
        result = analyze(valid, invalid)
        self.assertEqual(result["policies"][0]["traces"], 1)
        self.assertEqual(result["policies"][0]["invalidTraces"], 1)
        self.assertFalse(result["policies"][0]["coverageComplete"])

    def test_recognizable_raw_trace_missing_dispatch_timestamp_is_invalid(self):
        trace = fixture()
        trace.pop("dispatchBeginNs")
        result = MODULE.analyze([("raw", trace)])
        self.assertEqual(result["counts"]["foundTraces"], 1)
        self.assertEqual(result["counts"]["invalidTraces"], 1)

    def test_uint64_overflow_is_rejected_before_percentile_calculation(self):
        trace = fixture()
        trace["jobs"]["nodes"][0]["startNs"] = 1 << 128
        group = analyze(trace)["policies"][0]
        self.assertEqual(group["invalidNodes"], 1)

    def test_malformed_nested_data_returns_nonzero_cli_status(self):
        node = fixture()
        node["jobs"]["nodes"][0]["startNs"] = 0
        unit = fixture()
        unit["units"][0]["beginNs"] = 0
        jobs = fixture()
        jobs["jobs"]["available"] = 1
        owner = fixture()
        owner["ownerWait"]["spinElapsedNs"] = 10000
        worker = fixture()
        worker["workerSpin"]["activeSpinners"] = 3
        for trace in (node, unit, jobs, owner, worker):
            with self.subTest(trace=trace):
                self.assertEqual(cli_exit(trace), 1)

    def test_accepted_worker_scope_requires_its_axis_and_enabled_policy(self):
        axis, policy = fixture(), fixture(offset=2000000000)
        axis["workerSpin"]["axis"] = "per-view-cpu-time"
        policy["policy"]["workerSpinUs"] = 0
        result = analyze(axis, policy)
        self.assertEqual(result["counts"]["invalidSpinSnapshots"], 2)
        self.assertEqual(result["workerSpinFrames"], [])

    def test_json_and_ndjson_input_and_malformed_line(self):
        lines = "\ufeff" + "\n".join(json.dumps(wrapped(fixture())) for _ in range(2))
        result = MODULE.analyze(MODULE.parse_documents(lines, "capture.jsonl"))
        self.assertEqual(result["counts"]["documents"], 2)
        self.assertEqual(result["counts"]["duplicateTraces"], 1)
        with self.assertRaisesRegex(ValueError, "invalid JSON/NDJSON"):
            MODULE.parse_documents('{"result": {}}\nnot-json', "invalid.jsonl")

    def test_serialized_command_json_is_found_without_executing_content(self):
        result = MODULE.analyze([("capture", {"responseBody": json.dumps(wrapped(fixture()))})])
        self.assertEqual(result["counts"]["foundTraces"], 1)
        self.assertEqual(result["policies"][0]["metricsNs"]["nodeBody"]["count"], 2)

    def test_empty_capture_is_explicitly_unavailable(self):
        result = MODULE.analyze([("capture", {"result": {"nativeRecord": {}}})])
        self.assertEqual(result["counts"]["foundTraces"], 0)
        self.assertEqual(result["policies"], [])
        self.assertTrue(result["warnings"])


class RecordingTopologyTests(unittest.TestCase):
    def test_schema2_blocking_policy_preserves_timing_coverage_without_spin(self):
        result = analyze(fixture2())
        group = result["policies"][0]
        self.assertFalse(result["warnings"])
        self.assertTrue(group["coverageComplete"])
        self.assertTrue(group["completionPathCoverageComplete"])
        self.assertEqual(group["ownerWaitUnavailable"], 0)
        self.assertEqual(group["policy"]["ownerWaitMode"], "blocking")
        self.assertNotIn("ownerSpinWall", group["metricsNs"])
        self.assertNotIn("ownerSpinUs", group["policy"])
        self.assertEqual(result["workerSpinFrames"], [])
        self.assertNotIn("worker spin:", MODULE.format_text(result).lower())
        self.assertEqual(group["metricsNs"]["lastNodeCompleteToGraphComplete"]["p50"], 20)

    def test_exact_topology_maps_units_and_observed_completion_path(self):
        result = analyze(fixture2())
        entry = result["traces"][0]
        path = entry["completionPath"]
        self.assertEqual(path["status"], "available")
        self.assertEqual(path["axis"], MODULE.COMPLETION_PATH_AXIS)
        self.assertEqual(path["nodeIds"], [0, 1])
        self.assertEqual(path["lastCompletedNodes"], [1])
        self.assertFalse(path["hasTies"])
        self.assertIsNone(path["steps"][0]["readyAfterLatestPredecessorNs"])
        self.assertEqual(path["steps"][1]["readyAfterLatestPredecessorNs"], 10)
        self.assertEqual(path["steps"][1]["selectedPredecessorReasons"], ["cpu-producer"])
        self.assertEqual(path["steps"][0]["units"][0]["name"], "Producer")
        self.assertEqual(path["steps"][0]["units"][0]["callbackTotalNs"], 20)
        self.assertEqual(path["steps"][1]["executeOsThread"], 300)
        self.assertEqual(entry["completionObservation"]["lastCompleteNs"], 1000000380)

    def test_latest_observed_predecessor_is_selected_at_a_join(self):
        result = analyze(fork_fixture2())
        path = result["traces"][0]["completionPath"]
        self.assertFalse(result["warnings"])
        self.assertEqual(path["nodeIds"], [1, 2])
        self.assertEqual(path["steps"][-1]["latestPredecessorCandidates"], [1])
        self.assertEqual(path["steps"][-1]["readyAfterLatestPredecessorNs"], 15)

    def test_predecessor_completion_ties_are_explicit(self):
        trace = fork_fixture2()
        trace["jobs"]["nodes"][0]["completeNs"] = trace["jobs"]["nodes"][1]["completeNs"]
        result = analyze(trace)
        path = result["traces"][0]["completionPath"]
        self.assertFalse(result["warnings"])
        self.assertTrue(path["hasTies"])
        self.assertEqual(path["nodeIds"], [0, 2])
        self.assertEqual(path["predecessorTies"], [dict(node=2, predecessors=[0, 1])])

    def test_last_node_completion_ties_are_not_hidden(self):
        trace = fixture2()
        trace["jobs"]["nodes"][1].update(dependencies=[], dispatcherRoute=True)
        trace["jobs"]["nodes"][0]["completeNs"] = trace["jobs"]["nodes"][1]["completeNs"]
        trace["jobs"]["lastCompletedNodes"] = [0, 1]
        result = analyze(trace)
        path = result["traces"][0]["completionPath"]
        self.assertFalse(result["warnings"])
        self.assertTrue(path["hasTies"])
        self.assertEqual(path["lastCompletedNodes"], [0, 1])
        self.assertEqual(path["selectedLastNode"], 0)

    def test_missing_or_incorrect_last_completion_claim_is_invalid(self):
        missing, wrong, duplicate = fixture2(), fixture2(), fixture2()
        missing["jobs"].pop("lastCompletedNodes")
        wrong["jobs"]["lastCompletedNodes"] = [0]
        duplicate["jobs"]["lastCompletedNodes"] = [1, 1]
        for trace in (missing, wrong, duplicate):
            with self.subTest(trace=trace):
                result = analyze(trace)
                self.assertEqual(result["counts"]["invalidTraces"], 1)
                self.assertEqual(result["traces"][0]["completionPath"]["status"], "invalid")
                self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])

    def test_missing_topology_is_unavailable_while_last_completion_remains_observed(self):
        result = analyze(without_topology(fixture2()))
        group = result["policies"][0]
        entry = result["traces"][0]
        self.assertFalse(result["warnings"])
        self.assertTrue(group["coverageComplete"])
        self.assertFalse(group["completionPathCoverageComplete"])
        self.assertEqual(entry["completionObservation"]["lastCompletedNodes"], [1])
        self.assertEqual(entry["completionPath"]["status"], "unavailable-for-this-path")
        self.assertIsNone(entry["completionPath"]["steps"])

    def test_truncated_topology_never_becomes_a_complete_path(self):
        trace = without_topology(fixture2(), truncated=True)
        trace["unitTimingsTruncated"] = True
        trace["units"].pop()
        result = analyze(trace)
        self.assertFalse(result["warnings"])
        self.assertEqual(result["traces"][0]["completionPath"]["status"], "topology-truncated")
        self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])
        self.assertFalse(result["policies"][0]["coverageComplete"])
        self.assertFalse(result["policies"][0]["completionPathCoverageComplete"])

    def test_unavailable_jobs_do_not_invent_last_completion_or_spin_fields(self):
        trace = without_topology(fixture2())
        trace["jobs"] = dict(available=False, nodes=[], topologyAvailable=False, topologyTruncated=False,
                             lastCompletedNodes=[])
        trace.update(dependencyGraph=False, batchSubmit=False, waitBeginNs=0)
        result = analyze(trace)
        group = result["policies"][0]
        self.assertFalse(result["warnings"])
        self.assertEqual(result["counts"]["invalidTraces"], 0)
        self.assertNotIn("lastNodeCompleteToGraphComplete", group["metricsNs"])
        self.assertEqual(group["ownerWaitUnavailable"], 0)
        self.assertIsNone(result["traces"][0]["completionObservation"]["lastCompleteNs"])

    def test_schema2_timing_disabled_has_no_synthetic_path_or_wall_samples(self):
        trace = fixture2()
        trace.update(timingEnabled=False, dispatchBeginNs=0, waitBeginNs=0, returnNs=0)
        result = analyze(trace)
        self.assertFalse(result["warnings"])
        self.assertEqual(result["policies"][0]["metricsNs"], {})
        self.assertEqual(result["traces"][0]["completionPath"]["status"], "timing-disabled")
        self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])

    def test_empty_native_graph_is_distinct_from_unavailable_topology(self):
        trace = fixture2()
        trace["jobs"].update(nativeTasks=0, nodes=[], dispatcherBeginNs=0, dispatcherHandoffs=0,
                             lastCompletedNodes=[])
        trace["units"] = []
        result = analyze(trace)
        self.assertFalse(result["warnings"])
        self.assertEqual(result["traces"][0]["completionPath"]["status"], "empty-graph")
        self.assertEqual(result["traces"][0]["completionPath"]["nodeIds"], [])
        self.assertIsNone(result["traces"][0]["completionObservation"]["lastCompleteNs"])
        self.assertNotIn("lastNodeCompleteToGraphComplete", result["policies"][0]["metricsNs"])

    def test_partial_node_timing_cannot_verify_the_last_node(self):
        trace = without_topology(fixture2(), truncated=True)
        trace["jobTimingsTruncated"] = True
        trace["jobs"].update(nativeTasks=3, lastCompletedNodes=[2])
        result = analyze(trace)
        self.assertFalse(result["warnings"])
        self.assertEqual(result["traces"][0]["completionObservation"]["status"], "node-timings-truncated")
        self.assertIsNone(result["traces"][0]["completionObservation"]["lastCompletedNodes"])
        self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])
        self.assertNotIn("lastNodeCompleteToGraphComplete", result["policies"][0]["metricsNs"])

    def test_missing_duplicate_forward_and_self_dependencies_are_rejected(self):
        missing, duplicate, forward, self_edge = (fixture2() for _ in range(4))
        missing["jobs"]["nodes"][1].pop("dependencies")
        duplicate["jobs"]["nodes"][1]["dependencies"] *= 2
        forward["jobs"]["nodes"][0]["dependencies"] = [dict(node=1, reasons=["cpu-producer"])]
        self_edge["jobs"]["nodes"][0]["dependencies"] = [dict(node=0, reasons=["cpu-producer"])]
        for trace in (missing, duplicate, forward, self_edge):
            with self.subTest(trace=trace):
                result = analyze(trace)
                self.assertEqual(result["counts"]["invalidTraces"], 1)
                self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])

    def test_unsorted_dependency_ids_are_rejected(self):
        trace = fork_fixture2()
        trace["jobs"]["nodes"][2]["dependencies"].reverse()
        self.assertEqual(analyze(trace)["counts"]["invalidTraces"], 1)

    def test_dependency_reason_validation_is_strict(self):
        for reasons in ([], ["gpu-fence"], ["cpu-producer", "cpu-producer"], [False], "cpu-producer"):
            with self.subTest(reasons=reasons):
                trace = fixture2()
                trace["jobs"]["nodes"][1]["dependencies"][0]["reasons"] = reasons
                self.assertEqual(analyze(trace)["counts"]["invalidTraces"], 1)

    def test_same_target_and_cpu_producer_reasons_can_share_one_edge(self):
        trace = fixture2()
        trace["jobs"]["nodes"][1]["target"] = 0
        trace["units"][1]["target"] = 0
        trace["jobs"]["nodes"][1]["dependencies"][0]["reasons"] = ["same-target", "cpu-producer"]
        result = analyze(trace)
        self.assertFalse(result["warnings"])
        self.assertEqual(result["traces"][0]["completionPath"]["nodeIds"], [0, 1])

    def test_same_target_dependency_must_match_and_include_the_previous_node(self):
        mismatch, missing = fixture2(), fixture2()
        mismatch["jobs"]["nodes"][1]["dependencies"][0]["reasons"] = ["same-target"]
        missing["jobs"]["nodes"][1]["target"] = 0
        missing["units"][1]["target"] = 0
        for trace in (mismatch, missing):
            with self.subTest(trace=trace):
                self.assertEqual(analyze(trace)["counts"]["invalidTraces"], 1)

    def test_successor_cannot_be_ready_before_a_predecessor_completes(self):
        trace = fixture2()
        trace["jobs"]["nodes"][0]["completeNs"] = trace["jobs"]["nodes"][1]["readyNs"] + 1
        result = analyze(trace)
        self.assertEqual(result["policies"][0]["invalidNodes"], 0)
        self.assertEqual(result["policies"][0]["invalidTopologyTraces"], 1)
        self.assertIsNone(result["traces"][0]["completionPath"]["steps"])

    def test_missing_duplicate_and_out_of_range_unit_maps_are_rejected(self):
        missing, duplicate, outside = (fixture2() for _ in range(3))
        missing["jobs"]["nodes"][1]["units"] = []
        duplicate["jobs"]["nodes"][1]["units"] = [1, 1]
        outside["jobs"]["nodes"][1]["units"] = [2]
        for trace in (missing, duplicate, outside):
            with self.subTest(trace=trace):
                self.assertEqual(analyze(trace)["counts"]["invalidTraces"], 1)

    def test_unit_identity_reverse_mapping_and_target_wave_must_match(self):
        for field, value in (("unit", 0), ("node", 0), ("target", 7), ("wave", 7)):
            with self.subTest(field=field):
                trace = fixture2()
                trace["units"][1][field] = value
                result = analyze(trace)
                self.assertEqual(result["counts"]["invalidTraces"], 1)
                self.assertIsNone(result["traces"][0]["completionPath"]["nodeIds"])

    def test_unobserved_skipped_unit_keeps_mapping_without_zero_callback_sample(self):
        trace = fixture2()
        trace["jobs"]["nodes"][1]["skipped"] = True
        trace["units"][1].update(beginNs=0, endNs=0, callbackNs=0, callbackInvocations=0)
        result = analyze(trace)
        path = result["traces"][0]["completionPath"]
        self.assertFalse(result["warnings"])
        self.assertEqual(path["status"], "available")
        self.assertTrue(path["steps"][1]["skipped"])
        self.assertIsNone(path["steps"][1]["units"][0]["recordBodyWallNs"])
        self.assertIsNone(path["steps"][1]["units"][0]["callbackTotalNs"])
        self.assertEqual(result["policies"][0]["metricsNs"]["unitCallbackTotal"]["count"], 1)

    def test_topology_flags_must_be_boolean_and_consistent(self):
        for updates in (dict(topologyAvailable=1), dict(topologyTruncated=1), dict(topologyTruncated=True)):
            with self.subTest(updates=updates):
                trace = fixture2()
                trace["jobs"].update(updates)
                self.assertEqual(analyze(trace)["counts"]["invalidTraces"], 1)
        legacy = fixture2()
        legacy["batchSubmit"] = False
        self.assertEqual(analyze(legacy)["counts"]["invalidTraces"], 1)

    def test_topology_fields_must_not_survive_when_topology_is_unavailable(self):
        trace = fixture2()
        trace["jobs"]["topologyAvailable"] = False
        result = analyze(trace)
        self.assertEqual(result["counts"]["invalidTraces"], 1)
        self.assertIsNone(result["traces"][0]["completionPath"]["steps"])

    def test_schema2_rejects_removed_spin_sections_and_old_policy(self):
        for field, value in (("ownerWait", {}), ("workerSpin", {}), ("policy", fixture()["policy"])):
            with self.subTest(field=field):
                trace = fixture2()
                trace[field] = value
                result = analyze(trace)
                self.assertEqual(result["counts"]["invalidTraces"], 1)
                self.assertEqual(result["policies"], [])

    def test_schema_versions_keep_separate_policies_and_legacy_spin_statistics(self):
        result = analyze(fixture(), fixture2(offset=2000000000))
        self.assertFalse(result["warnings"])
        self.assertEqual(len(result["policies"]), 2)
        self.assertEqual({group["policy"]["schemaVersion"] for group in result["policies"]}, {1, 2})
        self.assertEqual(len(result["workerSpinFrames"]), 1)
        self.assertEqual(result["traces"][0]["completionPath"]["status"], "unsupported-schema-1")

    def test_schema2_conflicting_topology_is_quarantined_without_legacy_spin_counts(self):
        first, second = fixture2(), fixture2()
        second["jobs"]["nodes"][1]["dependencies"] = []
        result = analyze(first, second)
        self.assertEqual(result["counts"]["conflictingTraces"], 1)
        self.assertEqual(result["counts"]["invalidTraces"], 1)
        self.assertEqual(result["counts"]["quarantinedSpinSnapshots"], 0)
        self.assertEqual(result["policies"][0]["metricsNs"], {})

    def test_schema2_repeated_queries_are_not_additional_path_samples(self):
        trace = fixture2()
        result = analyze(trace, copy.deepcopy(trace))
        self.assertEqual(result["counts"]["duplicateTraces"], 1)
        self.assertEqual(result["policies"][0]["topologyAvailableTraces"], 1)
        self.assertEqual(len(result["traces"]), 1)

    def test_schema2_invalid_topology_returns_nonzero_cli_status(self):
        trace = fixture2()
        trace["jobs"]["nodes"][1]["dependencies"] *= 2
        self.assertEqual(cli_exit(trace), 1)


if __name__ == "__main__":
    unittest.main()
