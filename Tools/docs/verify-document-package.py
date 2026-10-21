"""Check that the built documentation contains its inputs and portable links."""
import hashlib,json,pathlib,re,posixpath
from html.parser import HTMLParser
from urllib.parse import unquote,urlsplit
ROOT=pathlib.Path(__file__).resolve().parents[2];DOCS=ROOT/'docs';DIST=DOCS/'dist'
failures=[];count=0;links=0
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class Page(HTMLParser):
 def __init__(self,text):super().__init__(convert_charrefs=True);self.links=[];self.feed(text)
 def handle_starttag(self,tag,attrs):
  self.links.extend(v for k,v in attrs if k in ('href','src'))
for path in DOCS.rglob('*'):
 if not path.is_file():continue
 rel=path.relative_to(DOCS).as_posix()
 if rel.split('/')[0] in ('dist','src','node_modules') or rel in ('index.html','api/index.html') or re.search(r'\.sheet(?:-\d+)?\.png$|\.html\.\d+\.tmp$',rel):continue
 target=DIST/rel;count+=1
 if not target.exists() or digest(target)!=digest(path):failures.append('Missing/stale deployed file: '+rel)
for path in DIST.rglob('*.html'):
 if path.name.endswith('.src.html'):continue
 for href in Page(path.read_text(encoding='utf-8-sig')).links:
  uri=urlsplit(href)
  if uri.scheme or href.startswith('//') or not uri.path:continue
  target=DIST/unquote(uri.path).lstrip('/') if uri.path.startswith('/') else path.parent/unquote(uri.path)
  links+=1
  if not target.exists():failures.append('Broken deployed link: '+path.relative_to(DIST).as_posix()+' -> '+href)
for path in DIST.rglob('*'):
 if path.is_file() and re.search(r'\.sheet(?:-\d+)?\.png$|\.html\.\d+\.tmp$',path.name):failures.append('Inspection-only file deployed: '+str(path))
for name in ('CSharpRenderPipelinePlan','ScriptableRenderPipelinePlan'):
 for suffix in ('.md','.html','.src.html'):
  if (DIST/'plans'/(name+suffix)).exists():failures.append('Cancelled SRP plan deployed: '+name+suffix)
report={'filesCompared':count,'deployedLocalLinksInspected':links,'status':'PASS' if not failures else 'FAIL','problems':failures}
(ROOT/'Build/full-docs-package-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False));raise SystemExit(bool(failures))
