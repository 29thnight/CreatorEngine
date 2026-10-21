"""Validate the complete authored documentation corpus and fixed-master evidence."""
import collections
import hashlib
import json
import pathlib
import posixpath
import re
import subprocess
import sys
from html.parser import HTMLParser
from urllib.parse import unquote, urlsplit

ROOT = pathlib.Path(__file__).resolve().parents[2]
manifest = json.loads((ROOT/'docs/analysis/MasterDocumentationAudit20261020.json').read_text(encoding='utf-8'))
sha = manifest['master']
plans=json.loads((ROOT/'docs/plans/MasterPlanAudit20261020.json').read_text(encoding='utf-8'))
api=json.loads((ROOT/'docs/api/reference.json').read_text(encoding='utf-8'))
source_paths={e['path'] for d in manifest['documents'] for e in d['sources']}
source_paths.update(d['path'] for d in manifest['documents'])
source_paths.update(e['path'] for d in manifest['standalone'] for e in d.get('sources',[]))
source_paths.update(e['path'] for g in plans['evidence'].values() for e in g['sources'])
source_paths.update(api['meta']['sourceFiles'])
source_paths.update(p['source'] for p in api['pages'])
blobs={}; batch=subprocess.Popen(['git','cat-file','--batch'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
for path in sorted(source_paths):
    batch.stdin.write((sha+':'+path+'\n').encode()); batch.stdin.flush()
    header=batch.stdout.readline().decode().split()
    if header[-1]=='missing': raise ValueError('Missing source '+path)
    blobs[path]=(batch.stdout.read(int(header[-1])),header[0]); batch.stdout.read(1)
batch.stdin.close(); batch.wait()
problems = []
def check(ok, message):
    if not ok: problems.append(message)
def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT)
def digest(file): return hashlib.sha256((ROOT/file).read_bytes()).hexdigest()
def decode(raw):
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')): return raw.decode('utf-16')
    for encoding in ('utf-8-sig','cp949','latin-1'):
        try: return raw.decode(encoding)
        except UnicodeDecodeError: pass
class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids=[]; self.links=[]; self.h1s=0
        self.feed(text)
    def handle_starttag(self, tag, attrs):
        attrs=dict(attrs)
        if attrs.get('id'): self.ids.append(attrs['id'])
        if tag=='h1': self.h1s+=1
        for key in ('href','src'):
            if key in attrs: self.links.append(attrs[key])

current_master=git('rev-parse','master').decode().strip()
if '--allow-descendant-master' in sys.argv:
    check(subprocess.run(['git','merge-base','--is-ancestor',sha,current_master],cwd=ROOT).returncode==0,'Audited snapshot is not an ancestor of current master')
else:
    check(current_master==sha,'master changed during review')
dashboard='docs/RefactoringPlanDashboard.html'
check(digest(dashboard)==manifest['dashboardSha256'],'dashboard changed during full documentation review')
subprocess.run(['node','Tools/docs/collect-document-links.mjs'],cwd=ROOT,check=True)
markdown=json.loads((ROOT/'Build/document-links.json').read_text(encoding='utf-8'))
pages={p.relative_to(ROOT).as_posix():Page(p.read_text(encoding='utf-8-sig')) for p in (ROOT/'docs').rglob('*.html') if 'dist' not in p.parts}
all_links={p:x.links for p,x in pages.items()}
for file, tokens in markdown.items(): all_links[file]=tokens['links']+[link for text in tokens['html'] for link in Page(text).links]
bad=[]; inspected=0
for file,links in all_links.items():
    for link in links:
        try: parsed=urlsplit(link)
        except ValueError: problems.append('Malformed URL '+file+': '+link); continue
        if parsed.scheme or link.startswith('//'): continue
        if file.endswith('.src.html') and parsed.path=='kit.css': continue
        if parsed.path.startswith('/'): continue # application routes are tested separately
        inspected+=1
        dest=posixpath.normpath(posixpath.join(posixpath.dirname(file),unquote(parsed.path).replace('\\','/'))) if parsed.path else file
        if not (ROOT/dest).exists(): bad.append([file,link,'missing file']); continue
        if parsed.fragment and dest in pages and unquote(parsed.fragment) not in pages[dest].ids:
            bad.append([file,link,'missing anchor'])
for item in bad: problems.append('Broken link: '+repr(item))

sources={}; citations=0
def evidence(e):
    global citations
    path=e['path']
    if path not in sources:
        raw,blob=blobs[path]
        sources[path]=(raw,decode(raw).splitlines(),blob)
    raw,lines,blob=sources[path]
    check(hashlib.sha256(raw).hexdigest()==e['sha256'],'SHA mismatch: '+path)
    check(blob==e.get('blob',e.get('gitBlob')),'Git blob mismatch: '+path)
    for row in e['lines']:
        check(0<row['line']<=len(lines) and lines[row['line']-1].strip()==row['text'],'Citation mismatch: '+path+':'+str(row['line']))
        citations+=1
for doc in manifest['documents']:
    check(doc.get('baselineRevision')==sha,'Historical baseline revision mismatch: '+doc['path'])
    check(blobs[doc['path']][1]==doc.get('baselineGitBlob'),'Historical baseline blob mismatch: '+doc['path'])
    check(hashlib.sha256(blobs[doc['path']][0]).hexdigest()==doc.get('baselineSha256'),'Historical baseline SHA mismatch: '+doc['path'])
    for source in doc['sources']: evidence(source)
    check(sha in (ROOT/doc['path']).read_text(encoding='utf-8'),'Stale current section: '+doc['path'])
    check(sha in (ROOT/doc['html']).read_text(encoding='utf-8'),'Stale HTML: '+doc['html'])
for doc in manifest['standalone']:
    for source in doc.get('sources',[]):evidence(source)
    check(sha in (ROOT/doc['path']).read_text(encoding='utf-8'),'Stale standalone report: '+doc['path'])
check(plans['master']==sha,'Plan audit is on an older master')
for group in plans['evidence'].values():
    for source in group['sources']: evidence(source)
for doc in plans['documents']:
    check(sha in (ROOT/doc['html']).read_text(encoding='utf-8'),'Stale plan HTML: '+doc['html'])

check(api['meta']['revision']==sha,'API revision is stale')
for path,blob in api['meta']['sourceFiles'].items():
    check(blobs[path][1]==blob,'API source mismatch: '+path)
member_count=0
for page in api['pages']:
    raw,blob=blobs[page['source']]; lines=decode(raw).splitlines()
    check(blob==page['sourceBlob'],'API page source mismatch: '+page['id'])
    check(len(page.get('members',[]))==len(page['memberEvidence']),'API evidence coverage: '+page['id'])
    for member, record in zip(page.get('members',[]),page['memberEvidence']):
        member_count+=1
        check(member[0]==record['member'] and member[1]==record['matches'][0]['signature'],'API signature mismatch: '+page['id']+'/'+member[0])
        for match in record['matches']:
            check(match['sourceDeclaration'] in lines[match['line']-1],'API declaration mismatch: '+page['id']+'/'+member[0])
ids={p['id'] for p in api['pages']}
for old,new in api.get('aliases',{}).items(): check(new in ids,'API alias target missing: '+old)

visual_count=0
if '--visual' in sys.argv:
    for name,key,summary in [('Build/plan-audit-visual-final.json',plans,'docs/plans/MasterPlanAudit20261020.src.html'),('Build/full-docs-visual-final.json',manifest,'docs/analysis/MasterDocumentationAudit20261020.src.html')]:
        report=json.loads((ROOT/name).read_text(encoding='utf-8'))
        expected={d['html'].replace('.html','.src.html') for d in key['documents']}|{summary}
        check(report['master']==sha,'Visual report revision mismatch: '+name)
        check({x['file'] for x in report['results']}==expected,'Visual coverage mismatch: '+name)
        for row in report['results']:
            check(row['passed'] and row['visualChecked'],'Visual failure: '+row['file'])
            check(digest(row['file'])==row['sourceSha256'],'Source changed after visual verification: '+row['file'])
            check(digest(row['file'].replace('.src.html','.html'))==row['htmlSha256'],'HTML changed after visual verification: '+row['file'])
        visual_count+=len(report['results'])
    standalone=json.loads((ROOT/'Build/full-docs-standalone-visual-final.json').read_text(encoding='utf-8'))
    check(standalone['master']==sha,'Standalone visual revision mismatch')
    check({r['file'] for r in standalone['results']}=={d['source'] for d in manifest['standalone'] if d['source'].endswith('.src.html')},'Standalone visual coverage mismatch')
    for row in standalone['results']:
        check(row['passed'] and row['visualChecked'],'Standalone visual failure: '+row['file'])
        check(digest(row['file'])==row['sourceSha256'] and digest(row['html'])==row['htmlSha256'],'Standalone changed after visual verification: '+row['file'])
    visual_count+=len(standalone['results'])

original=ROOT/'Build/full-docs-inherited-links.json'
legacy=json.loads(original.read_text(encoding='utf-8')) if original.exists() else []
report={'master':sha,'currentMaster':current_master,'allowDescendantMaster':'--allow-descendant-master' in sys.argv,'status':'PASS' if not problems else 'FAIL','updatedMarkdownDocuments':len(manifest['documents'])+len(plans['documents']), 'standaloneReports':len(manifest['standalone']), 'apiPages':len(api['pages']),'apiMembers':member_count,'documentationFilesInspected':len(all_links),'localLinksInspected':inspected,'brokenLinks':bad,'originalReportedMissingLinks':len(legacy),'sourceFilesVerified':len(sources),'sourceCitationsVerified':citations,'visualPagesVerified':visual_count,'dashboardPreserved':digest(dashboard)==manifest['dashboardSha256'],'problems':problems}
(ROOT/'Build/full-docs-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('brokenLinks','problems')},ensure_ascii=False))
for item in problems[:40]: print(item)
sys.exit(bool(problems))
