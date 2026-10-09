"""Compare FG off/on external latency or presentation timestamps.

This is an importer, not an input-to-photon instrument. It requires original raw
artifacts, declared instrument/clock/axis, exact recorded real-frame identities,
and explicit acceptance limits. CPU markers and aggregate SDK counters are never
accepted as photon timestamps or presentation intervals. Source-only authoring.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics


def require(value, message):
    if not value:
        raise ValueError(message)


def finite(value, name):
    number = float(value)
    require(math.isfinite(number), 'Nonfinite ' + name)
    return number


def identity(row):
    values = tuple(int(row[key]) for key in ('realFrameId', 'publicationFrameId', 'viewId', 'sceneEpoch'))
    require(all(value > 0 for value in values), 'Missing real-frame identity')
    return values


def distribution(values):
    require(values, 'Empty measurement series')
    values = sorted(values)
    return dict(samples=len(values), mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[math.ceil(len(values) * .95) - 1], maximum=max(values))


def artifact(root, reference):
    path = (root / reference['path']).resolve()
    require(path.is_file(), 'Missing evidence artifact: ' + str(path))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    require(digest == reference['sha256'].lower(), 'Evidence SHA256 mismatch: ' + str(path))
    return path


def load_profiler():
    source = Path(__file__).resolve().parents[1] / 'regression' / 'summarize-material-profile.py'
    spec = importlib.util.spec_from_file_location('temporal_profile_summary', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def analyze(path):
    root = path.resolve().parent
    manifest = json.loads(path.read_text(encoding='utf-8-sig'))
    require(manifest['schema'] == 'temporal.external-measurement.v1', 'Unsupported evidence schema')
    kind = manifest['kind']
    require(kind in ('latency', 'pacing'), 'Expected latency or pacing evidence')
    source = manifest['source']
    require(all(isinstance(source.get(key), str) and source[key].strip() for key in
                ('instrument', 'instrumentId', 'toolVersion', 'clockId', 'frameAssociationMethod', 'method')),
            'Missing instrument, clock, version or frame-association details')
    require(source['timestampUnit'] == 'ms', 'Normalize calibrated source timestamps to milliseconds')
    physical = source['method'] in ('photodiode', 'high-speed-camera', 'ldat')
    if kind == 'latency':
        require(physical and source['axis'] == 'input-to-photon' and source['sharedClock'] is True,
                'Latency needs physical input/photon endpoints on a calibrated common clock; CPU/SDK markers are not enough')
        require(finite(source['clockUncertaintyMs'], 'clock uncertainty') >= 0, 'Negative clock uncertainty')
    else:
        require((physical and source['axis'] == 'physical-display') or
                (source['method'] == 'presentmon-displayed' and source['axis'] == 'os-displayed') or
                (source['method'] == 'sdk-present-timestamps' and source['axis'] == 'sdk-presentation'),
                'Pacing needs individual observed presentations; API call starts/returns and aggregate counts are insufficient')
    require(len(manifest['runs']) == 2 and {run['fgEnabled'] for run in manifest['runs']} == {False, True},
            'Exactly one FG-off and one FG-on run are required')
    require(all(type(run['fgEnabled']) is bool for run in manifest['runs']), 'FG enable values must be JSON booleans')
    policy = manifest['acceptance']
    minimum = int(policy['minimumSamples'])
    require(minimum >= 20, 'At least 20 independent samples/intervals per run are required')
    profiler = load_profiler()
    reports, compared = {}, []
    for run in manifest['runs']:
        label = 'on' if run['fgEnabled'] else 'off'
        profile_path = artifact(root, run['profile'])
        summary = profiler.analyze(profile_path)
        require(summary['realFramePerformanceGateEligible'], 'Run profile has incomplete/ambiguous/non-real provenance')
        profile = profiler.read_capture(profile_path)
        observation_path = artifact(root, run['presenterObservations'])
        presenter = {}
        for line in observation_path.read_text(encoding='utf-8-sig').splitlines():
            receipt = json.loads(line)
            if receipt.get('command') != 'temporal.status' or receipt['result']['status'] != 'succeeded':
                continue
            observed = receipt['result']['data'].get('presenterObservation')
            if not observed or observed['valid'] is not True:
                continue
            key = identity(observed)
            require(key not in presenter or presenter[key] == observed,
                    'Conflicting immutable presenter observations for one source frame')
            presenter[key] = observed
        real = {}
        for frame in profile['frames']:
            for sample in frame['measurements']:
                key = identity(sample)
                signature = tuple(sample[key] for key in ('renderWidth', 'renderHeight', 'displayWidth', 'displayHeight',
                                                          'upscaler', 'resolutionState', 'spatialMode', 'deepDvcApplied'))
                # Renderer provenance intentionally records no generated frames.
                # Actual FG mode comes only from this frame's presenter owner.
                previous = real.setdefault(key, signature)
                require(previous == signature, 'Conflicting provenance for one real-frame identity')
        raw_path = artifact(root, run['rawArtifact'])
        csv_path = artifact(root, run['samples'])
        require(isinstance(run.get('conversion'), str) and run['conversion'].strip(), 'Document raw-to-CSV conversion')
        with csv_path.open(newline='', encoding='utf-8-sig') as stream:
            rows = list(csv.DictReader(stream))
        require(rows, 'No external measurements')
        sample_ids, observed, values, signatures, conditions, source_keys = set(), set(), [], set(), set(), set()
        previous_timestamp = None
        for row in rows:
            sample_id = row['sampleId']
            require(sample_id and sample_id not in sample_ids, 'Missing/duplicate sample identity')
            sample_ids.add(sample_id)
            key = identity(row)
            require(key in real, 'External sample is not linked to this run\'s recorded real frame')
            require(key in presenter, 'External sample has no exact immutable presenter observation; polling gaps are unavailable')
            present = presenter[key]
            require(present['result']['status'] == 'success' and int(present['result']['nativeCode']) == 0 and
                    present['faultMode'] == 'none' and
                    present['nativeGateActive'] is False and int(present['requestGeneration']) > 0 and
                    int(present['requestGeneration']) == int(present['playerObservedGeneration']),
                    'Presenter observation is failed, faulted, capture-overridden or from mismatched source/presenter generations')
            require(present['provider'] in ('none', 'fsr', 'dlss', 'xess') and
                    (present['provider'] != 'none') == run['fgEnabled'] and
                    ((int(present['interpolatedFrameCount']) > 0) if run['fgEnabled'] else
                     int(present['interpolatedFrameCount']) == 0), 'Actual presenter mode disagrees with FG-off/on condition')
            conditions.add((present['provider'], int(present['interpolatedFrameCount']),
                            int(present['requestGeneration']), int(present['faultRevision'])))
            source_keys.add(key)
            signatures.add(real[key])
            if kind == 'latency':
                require(key not in observed, 'Duplicate latency trial for one real frame')
                observed.add(key)
                start = finite(row['inputTimestampMs'], 'input timestamp')
                end = finite(row['photonTimestampMs'], 'photon timestamp')
                require(end >= start, 'Photon endpoint precedes the input endpoint')
                values.append(end - start)
            else:
                frame_kind, ordinal = row['frameKind'], int(row['generatedOrdinal'])
                require((frame_kind == 'real' and ordinal == 0) or
                        (frame_kind == 'generated' and 0 < ordinal <= int(present['interpolatedFrameCount']) and
                         run['fgEnabled']), 'Invalid observed presentation identity')
                presented = key + (frame_kind, ordinal)
                require(presented not in observed, 'Duplicate observed presentation')
                observed.add(presented)
                timestamp = finite(row['presentationTimestampMs'], 'presentation timestamp')
                if previous_timestamp is not None:
                    require(timestamp > previous_timestamp, 'Presentation timestamps must be strictly increasing in source order')
                    values.append(timestamp - previous_timestamp)
                previous_timestamp = timestamp
        require(len(signatures) == 1, 'Mixed views/settings/extents within an external run')
        require(len(conditions) == 1, 'Presenter provider/count/generation/fault revision changed within the measured run')
        require(len({key[2:] for key in source_keys}) == 1,
                'External series crosses view or scene identity')
        require(len(values) >= minimum, 'Insufficient external measurements')
        if kind == 'pacing' and run['fgEnabled']:
            require(any(row['frameKind'] == 'generated' for row in rows), 'FG-on pacing has no observed generated presentations')
        compared.append(next(iter(signatures)))
        reports[label] = dict(distributionMs=distribution(values), profileSha256=summary['sha256'],
                              samplesSha256=run['samples']['sha256'], rawSha256=run['rawArtifact']['sha256'],
                              presenterObservationsSha256=run['presenterObservations']['sha256'],
                              presenterCondition=dict(zip(('provider', 'interpolatedFrameCount', 'requestGeneration', 'faultRevision'),
                                                          next(iter(conditions)))),
                              rawArtifact=str(raw_path), profile=str(profile_path), samples=str(csv_path),
                              runId=run['runId'], conversion=run['conversion'])
    require(compared[0] == compared[1], 'FG-off/on runs changed resolution, reconstruction, spatial mode or color effects')
    off, on = reports['off']['distributionMs'], reports['on']['distributionMs']
    if kind == 'latency':
        max_p95 = finite(policy['maximumOnP95Ms'], 'latency p95 limit')
        max_regression = finite(policy['maximumP95RegressionMs'], 'latency regression limit')
        uncertainty = finite(source['clockUncertaintyMs'], 'clock uncertainty')
        require(max_p95 > 0 and max_regression >= 0, 'Invalid latency limits')
        passed = on['p95'] + uncertainty <= max_p95 and on['p95'] - off['p95'] + 2 * uncertainty <= max_regression
    else:
        max_p95 = finite(policy['maximumOnP95IntervalMs'], 'pacing p95 limit')
        max_interval = finite(policy['maximumOnIntervalMs'], 'pacing worst-interval limit')
        require(max_interval >= max_p95 > 0, 'Invalid pacing limits')
        passed = on['p95'] <= max_p95 and on['maximum'] <= max_interval
    return dict(schema='temporal.external-result.v1', kind=kind, passed=passed, source=source, runs=reports,
                acceptance=policy, p95OnMinusOffMs=on['p95'] - off['p95'], physicalDisplayEvidence=physical,
                latencyAcceptance=bool(passed and kind == 'latency'),
                presentationPacingAcceptance=bool(passed and kind == 'pacing'),
                scope='imported-instrument-evidence-with-recorded-frame-association',
                provenance='Declared source and artifact hashes are retained; this importer does not independently certify an instrument')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.manifest)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    require(result['passed'], 'External measurement exceeds the declared acceptance limits')


if __name__ == '__main__':
    main()
