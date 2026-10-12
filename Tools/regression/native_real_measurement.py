"""Native live timing preflight for the RG8 consumer; never display FPS acceptance."""

def check(condition, message):
    if not condition:
        raise ValueError(message)


def verify_normal_temporal(row):
    """Reject old/unidentified timing rows instead of annotating them as native."""
    for name in ('temporalProvenance', 'gpuTemporalProvenance'):
        p = row.get(name, {})
        check(p.get('schemaVersion') == 2 and p.get('valid') is True and
              p.get('frameKind') == 'real' and p.get('generatedOrdinal') == 0 and
              p.get('upscaler') == p.get('frameGenerator') == 'none' and
              p.get('spatialMode') == 'off' and p.get('deepDvcApplied') is False and
              p.get('resolutionState') == 'native', 'Missing/ineligible native-real timing provenance: ' + name)
        for field in ('realFrameId', 'publicationFrameId', 'viewId', 'sceneEpoch',
                      'renderWidth', 'renderHeight', 'displayWidth', 'displayHeight'):
            value = p.get(field)
            # Command JSON encodes uint64 identities as decimal strings.
            check((type(value) is int or (type(value) is str and value.isascii() and value.isdecimal())) and
                  0 < int(value) <= 2**64 - 1,
                  'Missing native-real timing identity/extent: ' + name + '.' + field)
        check((p['renderWidth'], p['renderHeight']) == (p['displayWidth'], p['displayHeight']),
              'Normal timing is not at native extents')
    check(row.get('framesRenderedAxis') == 'completed-real-view-renders-not-display-presentations',
          'Unknown normal render throughput axis')
    p = row['gpuTemporalProvenance']
    check(int(p['publicationFrameId']) == row['gpu']['frame'] and int(p['viewId']) == row['gpu']['viewId'] and
          (p['displayWidth'], p['displayHeight']) == (row['display']['width'], row['display']['height']),
          'Native provenance does not identify the measured GPU view/frame/extent')


