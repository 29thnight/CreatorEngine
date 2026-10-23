import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { spawn, execFileSync } from 'node:child_process';

// Markdown and the reviewed manifest are the sources. Rendered HTML is disposable.
const root = process.cwd();
const args = process.argv.slice(2);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
let defaultMarked;
try { defaultMarked = createRequire(import.meta.url).resolve('marked'); }
catch { defaultMarked = path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/marked/lib/marked.esm.js'); }
const markedPath = option('--marked', defaultMarked);
const { marked } = await import(pathToFileURL(markedPath));
const fullMode = args.includes('--full-docs');
const summaryBase = fullMode ? 'docs/analysis/MasterDocumentationAudit20261020' : 'docs/plans/MasterPlanAudit20261020';
const data = JSON.parse(fs.readFileSync(fullMode ? summaryBase+'.json' : 'docs/plans/MasterPlanAudit20261020.json', 'utf8'));
const committedPaths = new Set(execFileSync('git',['ls-tree','-r','--name-only',data.master],{encoding:'utf8'}).trim().split(/\r?\n/));
if (fullMode) {
  data.documents = data.documents.map(d => ({...d, archived:false, owner:null, previousPath:d.path,
    requirements:[d.sourceMode === 'document references' ? '문서가 참조하는 현재 소스 선언·소유 경계를 대조했다. 이전 측정과 현행 구현을 구분한다.' : '연구·운영 기록의 유효 범위를 관련 runtime 경계와 구분한다.'],
    preservedAnchors:d.previousAnchors}));
}
const esc = s => String(s).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
const css = `main{display:block;max-width:76rem;margin:auto;padding:var(--s-6)}body{font-family:'IBM Plex Sans KR','Malgun Gothic',sans-serif}h1{font-size:var(--t-h2);max-width:48ch;margin-block:var(--s-3)}h2{margin-top:var(--s-7);margin-bottom:var(--s-4)}header{padding:0;margin:var(--s-4) 0}p,li{max-width:78ch;overflow-wrap:anywhere}p{margin-block:var(--s-3)}li{margin-bottom:var(--s-3)}a,code{overflow-wrap:anywhere}pre{overflow:auto}table{width:100%;border-collapse:collapse}td,th{padding:var(--s-3);text-align:left;vertical-align:top;border-bottom:1px solid var(--border)}figure{margin:var(--s-5) 0}details span[id]{display:block;scroll-margin-top:var(--s-6)}.table-scroll{overflow-x:auto}.audit-meta{display:block;font-size:var(--t-small);margin:var(--s-4) 0}@media(max-width:600px){main{padding:var(--s-4)}h1{font-size:var(--t-h2)}td,th{min-width:9rem}}`;
const nodes = {
  core: ['Job input', 'Scheduler', 'Workers', 'Completion', 'Join', 'Consumer'],
  scene: ['SceneManager', 'GC roots', 'Scene', 'Entity', 'Component', 'Collection'],
  asset: ['DataSystem', 'Resolver', 'Typed request', 'Payload owner', 'Frame pin', 'Retirement'],
  graph: ['Live pipeline', 'Graph versions', 'Dependency DAG', 'Recording tasks', 'Queue submit', 'Completion'],
  material: ['Document', 'Compiled program', 'Sealed inputs', 'Scene stages', 'GPU packets', 'RHI submit'],
  temporal: ['Camera snapshot', 'Temporal inputs', 'SDK provider', 'Submit history', 'Presenter', 'Provenance'],
  profile: ['Producers', 'Bounded records', 'Collector', 'Snapshot', 'Stream', 'Viewer'],
  editor: ['Editor window', 'Command', 'Transaction', 'Component', 'Dirty publish', 'Render proxy'],
  animation: ['Animator', 'AnimInstance', 'Pose tasks', 'Scheduler', 'Palette', 'Render frame'],
  physics: ['Scene', 'Simulation', 'Body / shape', 'Contact stream', 'PostPhysics', 'Managed code'],
  audio: ['AudioSource', 'AudioHost', 'Playback', 'Clip directory', 'Miniaudio', 'Completion'],
  script: ['Managed code', 'API / handle', 'SceneRuntime', 'Component', 'Dirty publish', 'Render proxy'],
  ui: ['UI component', 'UIManager', 'Canvas', 'Prepared data', 'UI pass', 'Submission'],
  build: ['Project settings', 'Build', 'Cook', 'Manifest / pak', 'Bootstrap', 'Player'],
  math: ['Math values', 'Transform', 'Camera', 'Snapshot', 'Boundary convert', 'Render / Physics'],
  serialize: ['Reflgen schema', 'Authoring', 'Cooked archive', 'Manifest / pak', 'Runtime reader', 'Consumer'],
  light: ['Scene geometry', 'UV / BVH', 'Bake jobs', 'Direct light', 'Indirect light', 'Lightmap'],
  network: ['Local scene', 'Simulation', 'Value snapshot', 'Wire identity', 'Replication', 'Transport'],
};
function diagram(group, caption) {
  const short = {'SceneManager':'Scene owner','Payload owner':'Payload pin','Graph versions':'Versions','Dependency DAG':'DAG','Recording tasks':'Record tasks','Compiled program':'Program','Sealed inputs':'Frame input','Camera snapshot':'Camera','Temporal inputs':'Frame input','Submit history':'History','Bounded records':'Records','Editor window':'Editor','Dirty publish':'Dirty delta','Render proxy':'Proxy','Clip directory':'Clip store','API / handle':'API handle','Value snapshot':'Snapshot','Wire identity':'Wire IDs','Project settings':'Settings','Manifest / pak':'Pak manifest','Cooked archive':'Cooked data','Runtime reader':'Reader','Boundary convert':'ABI convert','Render / Physics':'Consumer','Scene geometry':'Geometry','Indirect light':'Indirect'};
  const ns = nodes[group].map(n => short[n] ?? n);
  return `<figure><ve-flow label="${esc(caption)}">${ns.map((n, i) => `<ve-node id="n${i}" at="${i < 3 ? i + 1 : 6 - i} ${Math.floor(i / 3) + 1}"${i === 2 ? ' key' : ''}>${esc(n)}</ve-node>`).join('')}${ns.slice(1).map((_,i) => `<ve-edge from="n${i}" to="n${i+1}" label="${i === 2 ? 'handoff' : 'consumes'}"/>`).join('')}</ve-flow><figcaption>${esc(caption)}</figcaption></figure>`;
}
function page(title, body) {
  return `<!doctype html>\n<html lang="ko" data-register="instrument"><head><title>${esc(title)}</title><link rel="stylesheet" href="kit.css" data-ve-kit><style>${css}${fullMode ? 'h1,h2,h3,summary{overflow-wrap:anywhere}details{min-width:0;max-width:100%;overflow-x:auto}' : ''}</style></head><body><main>${body}</main></body></html>\n`;
}
const targets = [];
// Preserve the frozen audit evidence while allowing a later decision in the
// plan's Markdown to replace an obsolete generated headline.
const currentPlanSummaries = new Map();
for (const doc of data.documents) {
  let md = fs.readFileSync(doc.path, 'utf8').replace(/\r\n?/g,'\n').replaceAll('~','&#126;');
  const title = md.match(/^# (.*)$/m)[1];
  const currentSummary = md.match(/<!-- plan-summary:\s*([\s\S]*?)\s*-->/)?.[1]?.replace(/\s+/g, ' ').trim();
  if (currentSummary) currentPlanSummaries.set(doc.path, currentSummary);
  // Show remaining work first. The diagram records the implementation boundary.
  md = fullMode ? md.replace(/^# .*\n/,'').replaceAll(/<!-- master-doc-review:(?:start|end) -->/g,'') : md.slice(md.indexOf('## 남은 작업과 완료 판정'));
  let body = marked.parse(md).replaceAll(/href="([^"#]+)\.md(#[^"]*)?"/g, (match, target, fragment='') => {
    if (/^[a-z]+:/i.test(target)) return match;
    const destination = path.resolve(path.dirname(doc.path), target+'.md');
    const rendered = destination.replace(/\.md$/,'.html');
    if (fs.existsSync(rendered) || (destination.startsWith(path.resolve('docs')+path.sep) && fs.existsSync(destination)) || data.documents.some(d=>path.resolve(d.path)===destination)) return `href="${target}.html${fragment}"`;
    const repositoryPath = path.relative(root,destination).replaceAll('\\','/');
    return `href="https://github.com/29thnight/CreatorEngine/blob/${data.master}/${repositoryPath}${fragment}"`;
  });
  body = body.replaceAll(/href="([^"#]+)"/g,(match,target)=>{
    if (/^[a-z]+:|^\/\//i.test(target))return match;
    const [file,fragment=''] = target.split('#');
    const repositoryPath=path.relative(root,path.resolve(path.dirname(doc.path),decodeURIComponent(file.split('?')[0]))).replaceAll('\\','/');
    if (repositoryPath.startsWith('docs/')) {
      const normalized=path.relative(path.dirname(doc.path),repositoryPath).replaceAll('\\','/');
      return `href="${normalized}${target.slice(file.split('?')[0].length)}"`;
    }
    if (committedPaths.has(repositoryPath))return `href="https://github.com/29thnight/CreatorEngine/blob/${data.master}/${repositoryPath}${fragment?'#'+fragment:''}"`;
    if ([...committedPaths].some(p=>p.startsWith(repositoryPath.replace(/\/$/,'')+'/')))return `href="https://github.com/29thnight/CreatorEngine/tree/${data.master}/${repositoryPath}"`;
    return match;
  });
  if (fullMode) {
    const ids = new Set([...body.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]));
    const missing = doc.preservedAnchors.filter(id=>!ids.has(id) && !/^ve-|^n\d+$/.test(id));
    body += `<details><summary>이전 절 주소</summary><p>이전 절은 위 현행 대조와 펼친 원래 기록을 함께 읽는다.</p>${missing.map(id=>`<span id="${esc(id)}"></span>`).join('')}</details>`;
  }
  body = body.replaceAll('<table>', '<div class="ve-table table-scroll"><table>').replaceAll('</table>', '</table></div>');
  const dashboard = path.relative(path.dirname(doc.html),'docs/RefactoringPlanDashboard.html').replaceAll('\\','/');
  const summary = path.relative(path.dirname(doc.html),summaryBase+'.html').replaceAll('\\','/');
  const caption = ['light', 'network'].includes(doc.group) ? '기존 로컬 기반에서 후속으로 연결할 경계다. 전체 경로의 구현 완료를 뜻하지 않는다.' : '현재 구현 기반의 소유권 경계를 유지하며 남은 구현·제품 수용을 진행한다.';
  body = `<nav aria-label="문서 위치"><a href="${dashboard}">대시보드</a> · <a href="${summary}">전체 검증 원장</a> · <a href="${path.basename(doc.path)}">Markdown</a> · <a href="https://github.com/29thnight/CreatorEngine/blob/${data.master}/${doc.previousPath}">정리 전 원문</a></nav><header><p class="eyebrow">MASTER · ${fullMode ? 'SOURCE CONTRACT' : 'REMAINING PLAN'}</p><h1>${esc(title)}</h1><p>${esc(currentSummary ?? doc.requirements[0])}</p><p class="audit-meta">2026-10-20 · master <code>${data.master.slice(0,12)}</code> · ${fullMode ? '현행 선언·원래 기록 분리' : doc.archived || doc.owner ? '종료/승계' : '잔여 구현·수용'}</p></header>${diagram(doc.group,caption)}${body}`;
  targets.push([doc.html.replace(/\.html$/, '.src.html'), page(title, body)]);
}
if (!fullMode) {
const conflictTable = data.conflicts.map(c => `<tr><td>${esc(c.old)}</td><td>${esc(c.decision)}</td><td><a href="${c.owner}.html">${esc(c.owner)}</a></td></tr>`).join('');
const documentTable = data.documents.map(d => `<tr><td><a href="${path.relative('docs/plans',d.html).replaceAll('\\','/')}">${esc(path.basename(d.html,'.html'))}</a></td><td>${d.archived ? '종료/승계' : d.owner ? '단일 소유자로 인계' : '잔여 구현·수용'}</td><td>${d.owner ? `<a href="${d.owner}.html">${esc(d.owner)}</a>` : '자체 소유'}</td><td>${esc(currentPlanSummaries.get(d.path) ?? d.requirements.join(' '))}</td></tr>`).join('');
let summary = `<header><p class="eyebrow">CreatorEngine · 2026-10-20</p><h1>완료된 구현을 빼고 남은 수용을 분리했다</h1><p class="lead">master <code>${data.master}</code>의 배선을 기준으로 계획을 통일했다.</p><p><a href="../RefactoringPlanDashboard.html">대시보드</a> · <a href="MasterPlanAudit20261020.json">소스 해시·행 번호·문서 전수 원장</a></p></header>`;
summary += `<figure><ve-bars unit="문서" max="${data.documents.length}" label="${data.documents.length}개 문서의 구현 기반과 잔여 요구를 대조했다"><ve-bar value="${data.documents.filter(d=>!d.archived).length}" hot>활성·인계</ve-bar><ve-bar value="${data.documents.filter(d=>d.archived).length}">종료·승계</ve-bar></ve-bars><figcaption>대시보드가 직접 연결한 계획과 계획 간 연결 문서를 모두 대조했다.</figcaption></figure>`;
summary += `<section id="cancelled-srp"><h2>SRP 저작 전환은 취소했다</h2><p>${esc(data.cancelled.decision)}</p><p>구 SRP와 C# 저작 계획 파일·대시보드 15개 행을 제거했다. 활성 예정 공수 32일도 제외했다. 다른 대시보드 행과 상태는 보존했다. C++ Pass·그래프·슬롯 계약은 구현 정본을 유지한다.</p></section>`;
summary += diagram('scene','GC 객체 계층은 구현돼 있다. 남은 제품 수용을 GC 신설 작업과 분리한다.');
summary += `<section><h2>구현과 최신 결정으로 충돌을 정리했다</h2><div class="ve-table table-scroll"><table><thead><tr><th>충돌</th><th>채택한 방향</th><th>소유 계획</th></tr></thead><tbody>${conflictTable}</tbody></table></div></section>`;
summary += `<section><h2>각 계획에는 남은 요구와 고정 소스 근거를 남겼다</h2><div class="ve-table table-scroll"><table><thead><tr><th>문서</th><th>판정</th><th>소유자</th><th>남은 요구</th></tr></thead><tbody>${documentTable}</tbody></table></div></section>`;
summary += `<section><h2>검증 범위를 분명히 한다</h2><p>소스 파일 존재만으로 완료를 정하지 않고 객체 선언·소유 필드·요청·생명주기·실제 소비 경계를 대조했다. 근거에는 고정 master Git blob과 행 번호를 남겼다. 이전 절 링크는 완료 내용을 다시 계획하지 않는 인계 위치로 보존했다.</p><p>엔진 빌드·GPU 픽셀·SDK/지원 장치·실제 GUI·성능을 이번에 실행한 결과는 아니다. 과거 기록의 통과를 현재 master의 제품 수용으로 확대하지 않는다.</p></section><section class="sources"><h2>출처와 재검증</h2><p><a href="https://github.com/nicobailon/visual-explainer">visual-explainer</a>의 kit·Instrument 양식과 렌더 검사를 사용했다. <code>Tools/docs/render-master-plan-audit.mjs</code>로 HTML을 생성하고 <code>Tools/docs/verify-master-plan-audit.py</code>로 원장·master 근거·링크·취소 범위를 검사한다.</p></section>`;
targets.push(['docs/plans/MasterPlanAudit20261020.src.html',page('CreatorEngine master 계획 정리·검증',summary)]);
} else {
  const row = d => `<tr><td><a href="${path.relative('docs/analysis',d.html).replaceAll('\\','/')}">${esc(path.basename(d.html,'.html'))}</a></td><td>${esc(d.group)}</td><td>${esc(d.sourceMode)}</td><td>${d.sources.length}</td></tr>`;
  const summaries = data.documents.map(row).join('');
  const api = JSON.parse(fs.readFileSync(data.api,'utf8'));
  const totals = new Map();
  for (const d of data.documents) totals.set(d.group,(totals.get(d.group)??0)+1);
  let summary = `<header><p class="eyebrow">MASTER · API / DESIGN / ANALYSIS</p><h1>현재 구현과 과거 실행 기록을 분리했다</h1><p>기준 master <code>${data.master}</code>. 대시보드를 보존하며 API 선언과 문서별 소스 경계를 대조했다.</p><p><a href="MasterDocumentationAudit20261020.json">문서·소스·링크 처리 원장</a> · <a href="../api/">현재 API</a> · <a href="../RefactoringPlanDashboard.html">대시보드</a></p></header>`;
  summary += `<figure><ve-bars unit="문서" max="${data.documents.length}" label="문서별 현행 근거 대조">${[...totals].sort((a,b)=>b[1]-a[1]).map(([g,n])=>`<ve-bar value="${n}">${esc(g)}</ve-bar>`).join('')}</ve-bars><figcaption>각 문서의 참조 파일·현재 선언·소유 경계를 고정 master의 Git blob과 행으로 대조한다.</figcaption></figure>`;
  summary += `<section><h2>공개 선언이 바뀐 API를 바로잡았다</h2><p>Component의 제거된 충돌 훅은 ContactStream 등록/소비로 교체했다. 물리 쿼리는 sceneAnchor·버퍼·PhysicsQueryResult를 사용하며 현재 hit 타입은 PhysicsHit다. RaycastHit의 예전 문서 주소는 alias로 보존한다. SoundComponent의 개별 재생 handle과 RGHandle의 version/kind/epoch, RenderGraph 생성자 정책을 현재 선언으로 갱신했다.</p><p>선별 API ${api.pages.length}개 페이지의 ${api.pages.reduce((s,p)=>s+(p.members?.length??0),0)}개 멤버를 소스 선언으로 대조했다. 전체 엔진의 모든 내부 심볼을 자동 API로 공개한 것은 아니다.</p></section>`;
  summary += diagram('graph','선언 metadata와 frozen target 병렬 번역·제출은 구현 기반이다. 제품 비용 수용과 별도 저작 체계를 합치지 않는다.');
  summary += `<section><h2>기존 누락 732건을 분류하여 정리했다</h2><p>미보존 실행 산출물 679건, 이전 소스·문서 경로 52건, 코드 예제 오인식 1건이다. 이동한 소스·게시 보고서는 현행 경로로 연결하고 사라진 로컬 Build/Artifacts 파일은 원래 경로와 미보존 상태를 남긴다. 미보존에는 로컬에만 남아 저장소·배포본에 포함되지 않는 산출물도 포함한다. 과거 수치·해시·장치 조건은 현재 master의 실행 통과로 재발급하지 않는다.</p><p><a href="DocumentationLinkRemediation20261020.json">732건별 처리 원장</a> · <a href="MasterDocumentationAudit20261020.verification.json">최종 문서 검증 결과</a></p></section>`;
  summary += `<section><h2>문서별 현행 대조</h2><div class="ve-table table-scroll"><table><thead><tr><th>문서</th><th>경계</th><th>근거 종류</th><th>소스 파일</th></tr></thead><tbody>${summaries}</tbody></table></div></section>`;
  summary += `<section><h2>기존 독립 시각화와 측정 자료</h2><ul>${data.standalone.map(d=>`<li><a href="${path.relative('docs/analysis',d.path).replaceAll('\\','/')}">${esc(d.path)}</a></li>`).join('')}</ul></section><section class="sources"><p>visual-explainer kit의 Instrument 양식으로 생성했다. 이 원장은 소스·문서·링크 검증이며 엔진/GPU/SDK 실행 결과를 새로 주장하지 않는다.</p></section>`;
  targets.push([summaryBase+'.src.html',page('CreatorEngine 전체 문서 master 대조',summary)]);
}
for (const [file, text] of targets) {
  if (args.includes('--check')) {
    if (fs.readFileSync(file,'utf8') !== text) throw new Error('Source drift: '+file);
  } else if (!fs.existsSync(file) || fs.readFileSync(file,'utf8') !== text) fs.writeFileSync(file,text);
}
if (args.includes('--render')) {
  const kit = option('--kit','Build/visual-explainer-reference/plugins/visual-explainer/kit/render.mjs');
  const failures = [];
  const results = [];
  const priorPath = option('--results','');
  const prior = args.includes('--reuse') && fs.existsSync(priorPath) ? JSON.parse(fs.readFileSync(priorPath,'utf8')) : null;
  const hashFile = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const requested = option('--only','').split(',');
  const pending = targets.map(([file]) => file).filter(file => !args.includes('--only') || requested.some(part => file.includes(part)));
  const worker = async () => {
    while (pending.length) {
      const file = pending.shift();
      const cached = prior?.master === data.master ? prior.results.find(row=>row.file===file) : null;
      if (cached?.passed && cached.visualChecked && cached.sourceSha256===hashFile(file) && fs.existsSync(file.replace('.src.html','.html')) && cached.htmlSha256===hashFile(file.replace('.src.html','.html'))) {
        results.push(cached); continue;
      }
      let code, output, attempt = 0;
      do {
        attempt++;
        ({code,output} = await new Promise(resolve => {
          let output = '';
          const child = spawn(process.execPath,[kit,file,'--root',root,...(args.includes('--no-check')?['--no-check']:[])],{stdio:['ignore','pipe','pipe']});
          child.stdout.on('data', chunk => { output += chunk; });
          child.stderr.on('data', chunk => { output += chunk; });
          child.on('error', error => resolve({code:1,output:String(error)}));
          child.on('exit', code => resolve({code,output}));
        }));
        if (code && attempt < 3 && /ENOENT|EPERM|UNKNOWN: unknown error|kit's script did not run/.test(output)) {
          console.log(`Retrying transient render failure: ${file}`);
          await new Promise(resolve => setTimeout(resolve,500));
        } else break;
      } while (attempt < 3);
      process.stdout.write(output);
      if (code) failures.push(file);
      const sha256 = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
      results.push({file,attempts:attempt,passed:code===0,visualChecked:!args.includes('--no-check'),sourceSha256:sha256(file),htmlSha256:sha256(file.replace('.src.html','.html'))});
    }
  };
  await Promise.all(Array.from({length:Math.max(1,Math.min(3,Number(option('--jobs','1'))))},worker));
  if (args.includes('--results')) fs.writeFileSync(option('--results',''),JSON.stringify({master:data.master,results},null,2)+'\n');
  if (failures.length) throw new Error('Fix visual problems: '+failures.join(', '));
}
console.log(JSON.stringify({documents:data.documents.length,visualSources:targets.length,master:data.master}));
