import { readFileSync, writeFileSync } from 'node:fs';
import { marked } from 'marked';

const base = new URL('../../docs/design/', import.meta.url);
const md = readFileSync(new URL('DedicatedServerHandoff.md', base), 'utf8');
const sections = md.split(/\n## /).slice(1);
const flows = [
  ['현재 프레임 루프에서 고정 tick 경계로', ['현재 · 가변 delta', '고정 tick driver', '시작 / 완료 observer'], ['분리한다', '호출한다']],
  ['엔진 선행 작업이 담당자의 작업을 연다', ['ND-a · 계약', 'N3-b · 고정 tick', 'ND-b · 수명 / 쓰기'], ['Loopback을 연다', '복제를 연다']],
  ['서버와 플레이어는 simulation 기반을 공유한다', ['Server / Player host', 'Simulation / Scene', 'Replication / Transport'], ['구동한다', '값을 교환한다']],
  ['wire와 로컬 adapter를 구분한다', ['로컬 EntityHandle', 'Registry / schema', '고정폭 wire ID'], ['해석한다', '변환한다']],
  ['완료된 tick만 snapshot으로 공개한다', ['OnTickStarting', 'Physics / gameplay', 'OnTickCommitted'], ['명령을 적용한다', '결과를 확정한다']],
  ['큐에서 소유권을 넘기고 소비 후 반환한다', ['Network thread', 'Bounded queue / lease', 'Simulation thread'], ['값을 전달한다', '소비 후 반환한다']],
  ['서버는 window와 GPU 생성 없이 시작한다', ['Cooked content', 'Runtime services', 'Console server host'], ['scene을 제공한다', '고정 tick을 구동한다']],
  ['계약부터 두 클라이언트 통합까지 인수한다', ['계약 / tick / 수명', '포화 / 복구 / headless', '서버 + 두 client'], ['경계를 검증한다', '통합을 검증한다']]
];
const figure = (i) => {
  const [claim, nodes, verbs] = flows[i];
  return `<figure><div class="diagram"><svg class="ve-svg" viewBox="0 0 960 170" role="img" aria-label="${claim}"><path class="ve-e" d="M280 74 H340"/><path class="ve-e" d="M600 74 H660"/>${nodes.map((n,j)=>`<g class="ve-n ${j===1?'is-key':''}" data-ref="section-${i+1}"><rect class="sh" x="${20+j*320}" y="44" width="260" height="62" rx="4"/><text x="${150+j*320}" y="81" text-anchor="middle">${n}</text></g>`).join('')}<text class="ve-el" x="310" y="135" text-anchor="middle">${verbs[0]}</text><text class="ve-el" x="630" y="135" text-anchor="middle">${verbs[1]}</text></svg></div><figcaption>${claim}.</figcaption></figure>`;
};
const titles = ['현재 구조', '착수 순서', '모듈 경계', '인터페이스', '엔진 확장점', '큐와 소유권', 'Headless host', '인수 조건'];
const body = sections.map((s,i) => {
  const split = s.indexOf('\n');
  const title = s.slice(0,split).replace(/^\d+\. /,'');
  return `<section id="section-${i+1}"><div class="sec-head"><span class="ve-label">${titles[i]}</span><h2>${title}</h2></div>${figure(i)}<details${i===1?' open':''}><summary>상세 계약과 작업 항목 읽기</summary><div class="contract">${marked.parse(s.slice(split))}</div></details></section>`;
}).join('\n');
const html = `<!doctype html><html lang="ko" data-register="blueprint"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>데디케이티드 서버 인계 설계 · Creator Engine</title><link rel="stylesheet" href="kit.css" data-ve-kit><style>
body{font-family:'IBM Plex Sans','Malgun Gothic',sans-serif}main{max-width:1160px}a{color:var(--accent);text-underline-offset:.2em}h1{max-width:none;word-break:keep-all;font-size:clamp(2rem,4vw,3.8rem);line-height:1.18}section{scroll-margin-top:var(--s-8)}nav{display:flex;flex-wrap:wrap;gap:var(--s-3);padding:var(--s-4) 0;border-block:1px solid var(--border)}nav a{font-size:var(--t-small)}.diagram{overflow-x:auto;background:var(--surface);border:1px dashed var(--border-bright);border-radius:var(--r-2)}.diagram svg{min-width:900px;width:100%;display:block}.diagram text{font-size:18px}.diagram .ve-el{font-size:16px}details{border-top:1px solid var(--border);padding:var(--s-4) 0}summary{cursor:pointer;font-weight:600}.contract{padding-top:var(--s-4);overflow-x:auto}.contract p,.contract li{max-width:75ch}.contract table{min-width:720px}.contract code{overflow-wrap:anywhere}.sources a{overflow-wrap:anywhere}.status{display:flex;gap:var(--s-3);flex-wrap:wrap;margin:var(--s-4) 0}.handoff{display:grid;grid-template-columns:1fr 1fr;border:1px dashed var(--border-bright);margin:var(--s-4) 0}.handoff>div{padding:var(--s-4)}.handoff>div+div{border-left:1px dashed var(--border-bright)}.handoff strong{display:block;margin-bottom:var(--s-3)}@media(max-width:600px){.handoff{grid-template-columns:1fr}.handoff>div+div{border-left:0;border-top:1px dashed var(--border-bright)}}
</style></head><body><main><header><a href="../index.html">Creator Engine docs</a><p class="eyebrow">PHASE 20 · ENGINEERING HANDOFF · 2026-10-21</p><h1>엔진 경계를 먼저 세우고,<br>서버 구현을 넘깁니다.</h1><p class="lead">공유 simulation 위에 네트워크와 headless host를 연결하는 인계 설계.</p><div class="status"><span class="chip warn">설계안 · 구현 전</span><span class="chip info">N9 서버 host 포함</span></div><figure><div class="handoff"><div><strong>엔진 담당이 제공</strong>고정 tick · 엔티티 수명 · 필드 쓰기 통지<br>Runtime services · cooked content · 계약 게이트</div><div><strong>서버 담당이 구현</strong>Registry · replication · transport<br>Console host · 계측 · 통합 검증</div></div><figcaption>엔진은 실행 경계를 제공하고 서버 담당자는 그 경계 뒤의 구현을 소유합니다.</figcaption></figure></header><nav aria-label="문서 목차">${titles.map((t,i)=>`<a href="#section-${i+1}">${t}</a>`).join('')}</nav>${body}<section class="sources"><h2>근거와 원문</h2><p><a href="DedicatedServerHandoff.md">전체 Markdown 설계</a> · <a href="../plans/NetworkFrameworkPlan.html">PHASE 20 정본 계획</a> · <a href="https://github.com/nicobailon/visual-explainer">visual-explainer</a></p><p>코드 기준: <code>RuntimeFrame.cpp</code> · <code>EngineBootstrap.h</code> · <code>EngineLaunchConfig.h</code> · <code>Component.cpp</code> · <code>SceneRuntime.vcxproj</code></p></section></main></body></html>`;
writeFileSync(new URL('DedicatedServerHandoff.src.html',base),html);
console.log('Generated docs/design/DedicatedServerHandoff.src.html');
