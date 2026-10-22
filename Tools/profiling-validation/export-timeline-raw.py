"""Export retained native JSON and every ETW dumper row without HTML generation."""
import argparse,bisect,hashlib,json,statistics
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--root',required=True,type=Path);parser.add_argument('variant');args=parser.parse_args()
root=args.root.resolve();variant=args.variant;out=root/'json'/variant
out.mkdir(parents=True,exist_ok=True)
load=lambda path:json.loads(path.read_text('utf-8-sig'))
files=[];captures=[]
for mode in ['baseline','etw']:
 path=out/(mode+'-manifest.json')
 if path.exists():
  m=load(path);files.extend(m['files']);captures.append(m['capture'])
def save(name,value):
 target=out/name;target.parent.mkdir(parents=True,exist_ok=True)
 data=json.dumps(value,ensure_ascii=True,separators=(',',':'),allow_nan=False).encode('utf-8');target.write_bytes(data)
 files.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
def stats(values):
 values=sorted(values)
 return dict(count=len(values),mean=statistics.mean(values),p50=statistics.median(values),p95=values[min(len(values)-1,int(len(values)*.95))],p99=values[min(len(values)-1,int(len(values)*.99))],max=max(values)) if values else None
if not (root/variant/'creator-robot.etl').exists():
 for path in (root/variant).glob('*.json'):save('conditions/'+path.name,load(path))
 (out/'manifest.json').write_text(json.dumps(dict(format='CreatorEngine profiling JSON v2',compression=False,filtering=False,downsampling=False,captures=captures,etwAvailable=False,files=files),indent=2))
 raise SystemExit(0)
timeline = load(root/('timeline/'+variant+'-etw.json'))
save('etw/timeline-metadata.json', {k:v for k,v in timeline.items() if k != 'lanes'})
for tid, lane in timeline['lanes'].items():
    save('etw/thread-'+tid+'.json', lane)
scopes = {}
window = next(w for w in timeline['windows'] if w['name']=='on')
for lane in timeline['lanes'].values():
    states = lane['states']
    starts = [s[0] for s in states]
    for event in lane['scopes']:
        a, duration, marker = event[:3]
        b = a+duration
        if event[4]&3 or a < window['begin'] or b > window['end']:
            continue
        name = timeline['markers'][marker]
        record = scopes.setdefault(name, dict(wall=[], Running=0., Ready=0., Waiting=0., Unknown=0.))
        record['wall'].append(duration)
        for state in states[max(0,bisect.bisect_right(starts,a)-1):bisect.bisect_left(starts,b)]:
            delta = max(0, min(b,state[0]+state[1])-max(a,state[0]))
            record[{1:'Running',2:'Ready',3:'Waiting'}.get(state[2],'Unknown')] += delta
for record in scopes.values():
    count = len(record['wall'])
    record['wall'] = stats(record['wall'])
    record['meanStateMs'] = {k:record.pop(k)/count for k in ['Running','Ready','Waiting','Unknown']}
save('scope-metrics.json', scopes)

# Preserve every exported kernel CSV row, including header/schema rows, as JSON strings.
# Retain each original line so whitespace and variable ETW event layouts are unambiguous.
batch = []
kernelRows = 0
for line in (root/('timeline/'+variant+'-kernel.csv')).open(encoding='utf-8-sig'):
    batch.append(line.rstrip('\r\n'))
    kernelRows += 1
    if len(batch) == 50000:
        save(f'etw/kernel-lines-{kernelRows//50000:05}.json', batch)
        batch = []
if batch:
    save(f'etw/kernel-lines-{kernelRows//50000+1:05}.json', batch)
# Preserve the complete unfiltered xperf textual event export as well as the
# narrow kernel export used to derive scheduler states.
import subprocess
full = root/'timeline'/(variant+'-all-events.csv')
if not full.exists() or not full.stat().st_size:
    with (root/f'xperf-{variant}-all-events.log').open('w', encoding='utf-8') as diagnostics:
        subprocess.run(['C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit/xperf.exe',
                        '-i', str(root/variant/'creator-robot.etl'), '-o', str(full), '-a', 'dumper'],
                       stdout=diagnostics, stderr=subprocess.STDOUT, check=True)
allRows = 0
batch = []
reconstructedHash = hashlib.sha256()
def save_raw_rows(name, rows):
    save(name, rows)
    for row in load(out/name):
        reconstructedHash.update(row.encode('cp949', errors='surrogateescape'))
for line in full.open(encoding='cp949', errors='surrogateescape', newline=''):
    batch.append(line)
    allRows += 1
    if len(batch) == 50000:
        save_raw_rows(f'etw/all-events/lines-{allRows//50000:05}.json', batch)
        batch = []
if batch:
    save_raw_rows(f'etw/all-events/lines-{allRows//50000+1:05}.json', batch)
with full.open('rb') as stream:
    fullHash = hashlib.file_digest(stream, 'sha256').hexdigest()
assert reconstructedHash.hexdigest() == fullHash, 'ETW JSON byte round trip differs'
save('etw/all-events/source.json', dict(rows=allRows, bytes=full.stat().st_size,
    sha256=fullHash, providerFilter=None,
    encoding='cp949', errors='surrogateescape', jsonEscapes=True, lineEndingsPreserved=True, byteRoundTripEqual=True,
    policy='All xperf dumper rows including schemas and headers; original ETL retained separately.'))
for path in (root/variant).glob('*.json'):
    save('conditions/'+path.name, load(path))
save('etw/alignment-verdict.json',load(root/'timeline/alignment-verdict.json'))
save('etw/trace-statistics.json',dict(text=(root/variant/'trace-stats.txt').read_text('utf-8-sig')))
save('source-identity.json', load(root/'source-identity.json'))
save('source-verification.json',load(root/'source-verification.json'))

rates = {}
for name in ['baseline-live','baseline-off-live','etw-live','etw-off-live']:
    path = root/variant/(name+'.json')
    if path.exists():
        rates[name] = load(path)
save('measurement-windows.json', rates)
manifest = dict(format='CreatorEngine current-state profiling JSON v1', compression=False,
    filtering=False, downsampling=False, captures=captures, kernelExportRows=kernelRows, allProviderExportRows=allRows,
    integerPolicy='Exact integer tokens; use a uint64-aware JSON parser.',
    scope='All decoded CEPROF CPU/GPU events, counters, frame and render measurements; all unfiltered xperf dumper rows, the scheduler kernel export and derived per-thread ETW states. Original binary ETL is retained separately; raw ETL binary storage is not embedded.',
    gitHead=(root/'head.txt').read_text().strip(), files=files)
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf-8')
print(json.dumps(dict(captures=captures,files=len(files),bytes=sum(f['bytes'] for f in files),kernelRows=kernelRows)))
