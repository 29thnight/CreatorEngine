"""Validate the reviewed plan set against a fixed master, not dirty source files."""
import collections
import hashlib
import json
import pathlib
import re
import subprocess
import sys
from html import unescape
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / 'docs/plans/MasterPlanAudit20261020.json'
data = json.loads(DATA.read_text(encoding='utf-8'))
problems = []
def require(ok, message):
    if not ok: problems.append(message)

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def decode(raw):
    try: return raw.decode('utf-8-sig')
    except UnicodeDecodeError: return raw.decode('cp949')

class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids = []
        self.links = []
        self.figures = self.captions = self.h1s = 0
        self.feed(text)
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs: self.ids.append(attrs['id'])
        if tag == 'figure': self.figures += 1
        if tag == 'figcaption': self.captions += 1
        if tag == 'h1': self.h1s += 1
        for key in ('href', 'src'):
            if key in attrs: self.links.append(attrs[key])

def doclinks(file, text):
    if file.endswith('.html'): return Page(text).links
    text = re.sub(r'^```[^\n]*\n.*?^```[^\n]*$', '', text, flags=re.M|re.S)
    text = re.sub(r'`+[^`]*`+', '', text)
    return re.findall(r'\]\(([^)\s]+)(?:\s+[^)]*)?\)', text)

sha = data['master']
require(git('rev-parse', 'master').decode().strip() == sha, 'master moved; review the new implementation')
sources = {}
for group in data['evidence'].values():
    for e in group['sources']:
        blob = git('show', sha + ':' + e['path'])
        require(hashlib.sha256(blob).hexdigest() == e['sha256'], 'Source SHA mismatch: ' + e['path'])
        require(git('rev-parse', sha + ':' + e['path']).decode().strip() == e['gitBlob'], 'Git blob mismatch: ' + e['path'])
        lines = decode(blob).splitlines()
        for row in e['lines']:
            require(lines[row['line'] - 1].strip() == row['text'], 'Source citation changed: ' + e['path'])
        sources[e['path']] = e['gitBlob']

for d in data['documents']:
    for file in (d['path'], d['html'], d['html'].replace('.html', '.src.html')):
        require((ROOT / file).is_file(), 'Missing audited document: ' + file)
    text = (ROOT / d['html']).read_text(encoding='utf-8')
    require(sha in text, 'Rendered master identity is stale: ' + d['html'])
    page = Page(text)
    require(page.h1s == 1, f'Expected one title: {d["html"]} ({page.h1s})')
    require(page.figures >= 1 and page.figures == page.captions, 'Figure/caption mismatch: ' + d['html'])
    ids = set(page.ids)
    require(set(d['preservedAnchors']) <= ids, 'Lost previous section anchors: ' + d['html'])
    require(len(ids) == len(page.ids), 'Duplicate anchors: ' + d['html'])
    md = (ROOT / d['path']).read_text(encoding='utf-8')
    require(sha in md, 'Missing source identity: ' + d['path'])
    require('## 남은 작업과 완료 판정' in md, 'Missing residual requirements: ' + d['path'])
    require(not re.search(r'\|\s*(?:done|stopped)\s*\|', md), 'Closed task still active: ' + d['path'])
    require(not re.search(r'(?:CSharpRenderPipelinePlan|ScriptableRenderPipelinePlan)\.(?:md|html)', md), 'Cancelled owner link: ' + d['path'])
    if d['owner']:
        require(any(x['path'].endswith('/' + d['owner'] + '.md') for x in data['documents']), 'Missing owner: ' + d['owner'])

dashboard = (ROOT / 'docs/RefactoringPlanDashboard.html').read_text(encoding='utf-8')
require(not re.search(r'CSRP-|SRP-[0-9G]|CSharpRenderPipelinePlan|ScriptableRenderPipelinePlan|phase-4\.6', dashboard), 'Cancelled SRP is still in dashboard')
# Parse TASKS in Node's isolated VM rather than trusting regex to parse JavaScript.
node = r"""
const fs=require('fs'),vm=require('vm');
function parse(s){const start=s.indexOf('const TASKS = [')+'const TASKS = '.length;let q='',esc=false,d=0,i=start;for(;i<s.length;i++){const c=s[i];if(q){if(esc)esc=false;else if(c.charCodeAt(0)===92)esc=true;else if(c===q)q='';continue;}if(c==='"'||c==="'"||c==='`'){q=c;continue;}if(c==='[')d++;if(c===']'&&!--d)break;}return vm.runInNewContext('('+s.slice(start,i+1)+')');}
const cp=require('child_process');console.log(JSON.stringify({old:parse(cp.execFileSync('git',['show',process.argv[1]+':docs/RefactoringPlanDashboard.html'],{encoding:'utf8',maxBuffer:1<<24})),current:parse(fs.readFileSync('docs/RefactoringPlanDashboard.html','utf8'))}));
"""
rows = json.loads(subprocess.check_output(['node', '-e', node, sha], cwd=ROOT))
expected = [t for t in rows['old'] if t['phase'] not in (4.6, 'archive-srp-asset')]
require(len(rows['old']) - len(expected) == data['cancelled']['removedTaskCount'], 'Unexpected cancellation count')
require([(x['phase'], x['id'], x['status'], x.get('days'), x.get('earnedDays')) for x in expected] ==
        [(x['phase'], x['id'], x['status'], x.get('days'), x.get('earnedDays')) for x in rows['current']],
        'An unrelated dashboard ID/state/effort/order changed')
for name in data['cancelled']['documents']:
    require(not any((ROOT / 'docs/plans' / (name + ext)).exists() for ext in ('.md', '.html', '.src.html')), 'Cancelled document remains: ' + name)

# Inspect every tracked documentation file plus all newly authored audit pages.
# Missing local Build artifacts already present in master are reported separately.
tracked = git('ls-tree', '-r', '--name-only', sha, 'docs').decode().splitlines()
baseline = {}
proc = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
for f in tracked:
    if not f.endswith(('.md', '.html')): continue
    proc.stdin.write((sha + ':' + f + '\n').encode()); proc.stdin.flush()
    header = proc.stdout.readline().decode(); size = int(header.split()[-1])
    baseline[f] = decode(proc.stdout.read(size)); proc.stdout.read(1)
proc.stdin.close(); proc.wait()
current = {p.relative_to(ROOT).as_posix(): p.read_text(encoding='utf-8-sig')
           for p in (ROOT / 'docs').rglob('*') if p.is_file() and p.suffix in ('.md', '.html') and 'dist' not in p.parts}
all_git_paths = set(git('ls-tree', '-r', '--name-only', sha).decode().splitlines())

def find_bad(documents, old=False):
    bad = set(); checked = 0; anchors = {}
    for f, text in documents.items():
        if f.endswith('.html'): anchors[f] = set(Page(text).ids)
    for f, text in documents.items():
        for link in doclinks(f, text):
            if link.startswith(('http:', 'https:', 'mailto:', 'data:', 'javascript:', 'codex:', 'vscode:')): continue
            if link == 'kit.css' and f.endswith('.src.html'): continue
            try: parsed = urlsplit(unescape(link))
            except ValueError: continue
            if parsed.scheme: continue
            raw = unquote(parsed.path)
            if not raw: dest = f
            elif raw.startswith('/'): continue
            else:
                dest = os_normalize(pathlib.PurePosixPath(f).parent / raw)
            checked += 1
            exists = dest in all_git_paths if old else (ROOT / dest).exists()
            if not exists and dest not in documents:
                bad.add((f, link, 'missing file')); continue
            if parsed.fragment and dest.endswith('.html') and dest in anchors and unquote(parsed.fragment) not in anchors[dest]:
                bad.add((f, link, 'missing anchor'))
    return bad, checked

def os_normalize(p):
    import posixpath
    return posixpath.normpath(str(p).replace('\\', '/'))

before, old_links = find_bad(baseline, True)
after, links = find_bad(current)
new = after - before
for item in sorted(new): problems.append('New link defect: ' + repr(item))
plan_link_defects = [item for item in after if item[0].startswith('docs/plans/')]
for item in plan_link_defects:
    if item not in new: problems.append('Plan link defect: ' + repr(item))
visual_count = None
if '--visual-report' in sys.argv:
    visual_path = pathlib.Path(sys.argv[sys.argv.index('--visual-report') + 1])
    visual = json.loads(visual_path.read_text(encoding='utf-8'))
    expected_visual = {d['html'].replace('.html', '.src.html') for d in data['documents']}
    expected_visual.add('docs/plans/MasterPlanAudit20261020.src.html')
    require(visual['master'] == sha, 'Visual checks use a different master')
    require({x['file'] for x in visual['results']} == expected_visual, 'Visual checks do not cover the complete plan set')
    for row in visual['results']:
        require(row['passed'] and row['visualChecked'], 'Visual check did not pass: ' + row['file'])
        for file, key in ((row['file'], 'sourceSha256'), (row['file'].replace('.src.html', '.html'), 'htmlSha256')):
            require(hashlib.sha256((ROOT / file).read_bytes()).hexdigest() == row[key], 'Page changed after visual check: ' + file)
    visual_count = len(visual['results'])
direct = set(re.findall(r'plans/[A-Za-z0-9_./-]+\.html', dashboard))
audited_html = {d['html'].removeprefix('docs/') for d in data['documents']}
require(direct <= audited_html, 'Dashboard plan escaped review: ' + repr(sorted(direct - audited_html)))
report = {'master': sha, 'planDocuments': len(data['documents']), 'sourceFilesVerified': len(sources),
          'sourceCitationsVerified': sum(len(e['lines']) for g in data['evidence'].values() for e in g['sources']),
          'preservedAnchors': sum(len(d['preservedAnchors']) for d in data['documents']),
          'dashboardRows': len(rows['current']), 'cancelledRows': len(rows['old']) - len(rows['current']),
          'allDocumentationFilesInspected': len(current), 'localLinksInspected': links,
          'inheritedLinkDefects': len(after & before), 'removedInheritedLinkDefects': len(before - after),
          'newLinkDefects': len(new), 'planLinkDefects': len(plan_link_defects), 'problems': problems,
          'visualPagesVerified': visual_count,
          'inheritedDetails': sorted(after & before)}
destination = ROOT / 'Build/plan-audit-verification.json'
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('inheritedDetails', 'problems')}, ensure_ascii=False))
for p in problems[:30]: print(p)
sys.exit(bool(problems))
