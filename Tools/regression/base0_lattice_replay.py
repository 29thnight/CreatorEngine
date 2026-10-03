"""Independent typed Lattice archive framing and real GPU consumer checks."""
import argparse
import json
from pathlib import Path
import struct
from base0_artifacts import load_capture, require, truth
from base0_camera_replay import seal


def records(data):
    require(len(data) >= 24 and data[:8] == b'CELXI001', 'Lattice archive framing')
    require(data == seal(bytearray(data)), 'Lattice archive checksum')
    version, count = struct.unpack_from('<II', data, 8)
    require(version == 1 and count <= 4096, 'Lattice archive version/count')
    offset, result = 16, []
    for _ in range(count):
        entry = dict(start=offset, program=offset + 48, coverage=offset + 56,
                     parameter_count=offset + 68, parameters=[], textures=[])
        parameter_count = struct.unpack_from('<I', data, offset + 68)[0]
        require(parameter_count <= 128, 'Lattice parameter count')
        offset += 72
        for _ in range(parameter_count):
            param_id, tag = struct.unpack_from('<QI', data, offset)
            require(tag <= 4, 'Lattice parameter tag')
            entry['parameters'].append(dict(id=param_id, tag=tag, offset=offset, value=offset + 12))
            offset += 12 + (24 if tag == 3 else 32 if tag == 4 else 8)
        overrides = struct.unpack_from('<I', data, offset)[0]
        require(overrides <= 64, 'Lattice texture override count')
        offset += 4 + overrides * 24
        owners = struct.unpack_from('<I', data, offset)[0]
        require(owners <= 64, 'Lattice owner count')
        offset += 4
        for _ in range(owners):
            entry['textures'].append(dict(offset=offset, asset=offset + 8, digest=offset + 24))
            offset += 32
        result.append(entry)
    require(offset == len(data) - 8, 'Lattice trailing/truncated record')
    return result


def prepare(capture, output):
    data = bytearray((capture / 'lattice-input.bin').read_bytes())
    entries = records(data)
    require(len(entries) == 1 and entries[0]['textures'], 'representative Lattice fixture closure')
    entry = entries[0]
    parameter = next((p for p in entry['parameters'] if p['id'] == 900 and p['tag'] == 2), None)
    require(parameter is not None, 'fixture exposed IOR parameter 900 missing')
    output.mkdir(parents=True, exist_ok=False)
    cases = {'truncated': data[:-1]}
    bad = bytearray(data); bad[70] ^= 1; cases['checksum'] = bad
    for name, offset, value in [('version', 8, 2), ('count', 12, 0xffffffff),
                                 ('coverage', entry['coverage'], 6),
                                 ('parameter-count', entry['parameter_count'], 0xffffffff),
                                 ('type', parameter['offset'] + 8, 99)]:
        bad = bytearray(data); struct.pack_into('<I', bad, offset, value); cases[name] = seal(bad)
    bad = bytearray(data); struct.pack_into('<d', bad, parameter['value'], float('nan')); cases['nonfinite'] = seal(bad)
    bad = bytearray(data); bad[entry['program']] ^= 1; cases['program'] = seal(bad)
    bad = bytearray(data); bad[entry['start'] + 32] ^= 1; cases['graph-asset'] = seal(bad)
    bad = bytearray(data); bad[entry['textures'][0]['digest']] ^= 1; cases['texture-content'] = seal(bad)
    bad = bytearray(data); bad[entry['textures'][0]['asset']] ^= 1; cases['texture-asset'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<Q', bad, parameter['offset'], 0xffff); cases['parameter-id'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<I', bad, parameter['offset'] + 8, 0)
    struct.pack_into('<Q', bad, parameter['value'], 1); cases['parameter-type'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<d', bad, parameter['value'], 1.1); cases['changed-material'] = seal(bad)
    for name, packet in cases.items():
        (output / (name + '.bin')).write_bytes(packet)
    return dict(prepared=list(cases), drawCount=len(entries), parameterId=900, savedValue=struct.unpack_from('<d', data, parameter['value'])[0])


def verify(original, changed, source_file):
    a, _, ap = load_capture(original)
    b, _, bp = load_capture(changed)
    require(b['latticeInputContract'] == 'lattice-instance-v1' and truth(b['latticeInputReplayed']), 'missing Lattice replay consumer')
    require((changed / 'lattice-input.bin').read_bytes() == source_file.read_bytes(), 'Lattice replay file bits changed')
    require(a['draws'][0]['lattice']['uniformBytes'] != b['draws'][0]['lattice']['uniformBytes'], 'typed material replay ignored')
    changes = {name: sum(x != y for x, y in zip(ap[name], bp[name])) for name in ap}
    require(sum(changes.values()) > 0, 'material replay did not affect GPU pixels')
    result = dict(passed=True, inputBytesExact=True, typedUniformsChanged=True, changedComponents=changes)
    (changed / 'lattice-replay-verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['prepare', 'verify'])
    parser.add_argument('original', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('source_file', type=Path, nargs='?')
    args = parser.parse_args()
    print(json.dumps(prepare(args.original, args.output) if args.operation == 'prepare'
                     else verify(args.original, args.output, args.source_file)))
