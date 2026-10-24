// docs/ 아래 Markdown 을 visual-explainer 양식의 HTML 로 옮긴다. 원본은 Markdown 이다.
//
//   node Tools/docs/render-markdown-docs.mjs            # 전부
//   node Tools/docs/render-markdown-docs.mjs docs/plans/TexturePipelinePlan.md
//
// 결과는 원본 옆의 같은 이름 .html 이다(PhysicsRedesignPlan.md → PhysicsRedesignPlan.html).
// Markdown 을 남기는 이유: 계획서·기억·검사 스크립트 수백 곳이 .md 경로를 가리킨다.
// HTML 을 손으로 고치지 말 것 — 다음 실행이 덮어쓴다. 고칠 곳은 Markdown 이다.

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, posix, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Marked } from 'marked';
import { resolveDocumentLink, renderDocumentLink } from './document-links.mjs';
import { BASE_CSS, BASE_SCRIPT, FAVICON, FONT_LINKS, MERMAID_SCRIPT, TOKENS, escapeHtml, statusClass } from './visual-kit.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const GENERATOR = 'Tools/docs/render-markdown-docs.mjs';

const git = (...args) => execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 });
const repositoryFiles = new Set(git('ls-files', '-z').split('\0'));
const toPosix = p => p.replace(/\\/g, '/');

// 추적·미추적(무시 제외) Markdown 전부. 지운 파일이 목록에 남지 않도록 실재를 확인한다.
function listMarkdown() {
  return git('ls-files', '-co', '--exclude-standard', '-z', '--', 'docs').split('\0')
    .filter(p => p.endsWith('.md') && !p.startsWith('docs/dist/') && existsSync(join(ROOT, p)));
}

// 파일별 마지막 커밋 날짜. git log 한 번으로 모두 얻는다(파일마다 부르면 수백 번이다).
function lastCommitDates() {
  const dates = new Map();
  let current = null;
  for (const line of git('log', '--format=@%cs', '--name-only', '--', 'docs').split('\n')) {
    if (line.startsWith('@')) current = line.slice(1);
    else if (line && !dates.has(line)) dates.set(line, current);
  }
  return dates;
}

const CATEGORY = [
  ['docs/plans/archive/', '보관 계획'], ['docs/plans/', '계획'], ['docs/analysis/', '분석'],
  ['docs/design/', '설계'], ['docs/', '문서'],
];
const categoryOf = p => CATEGORY.find(([prefix]) => p.startsWith(prefix))[1];

// GitHub 의 제목 앵커 규칙. 문서끼리 #앵커로 이어 두었으므로 같은 규칙을 지켜야 링크가 산다.
function slugger() {
  const seen = new Map();
  return text => {
    const base = text.toLowerCase().trim().replace(/[^\p{L}\p{M}\p{N}\s_-]/gu, '').replace(/\s/g, '-');
    const n = seen.get(base) ?? 0;
    seen.set(base, n + 1);
    return n ? `${base}-${n}` : base;
  };
}

const stripTags = html => html.replace(/<[^>]*>/g, '')
  .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');

function chip(text, cls) {
  return `<span class="chip ${cls}">${text}</span>`;
}

// 제목 끝 괄호 속에 상태 낱말이 있으면 칩으로 바꾼다: "T0 — 임포트 (구현 적용 · 부분 검증)".
function headingWithChip(html) {
  const m = html.match(/^(.*\S)\s*\(([^()]{1,40})\)\s*$/s);
  if (!m) return html;
  const cls = statusClass(stripTags(m[2]));
  return cls ? `${m[1]} ${chip(m[2], cls)}` : html;
}

function renderDocument(source, file, markdownSet) {
  const slug = slugger();
  let mermaidCount = 0;
  const outline = [];

  const marked = new Marked({ gfm: true });
  marked.use({
    renderer: {
      heading({ tokens, depth }) {
        const inner = this.parser.parseInline(tokens);
        const id = slug(stripTags(inner));
        outline.push({ depth, id, text: stripTags(inner) });
        return `<h${depth} id="${escapeHtml(id)}">${headingWithChip(inner)}</h${depth}>\n`;
      },
      link({ href, title, tokens }) {
        const text = this.parser.parseInline(tokens);
        return renderDocumentLink(resolveDocumentLink(href, file, { markdownSet, tracked: repositoryFiles }), text, title);
      },
      code({ text, lang }) {
        if ((lang || '').trim() === 'mermaid') {
          mermaidCount++;
          return `<pre class="mermaid">${escapeHtml(text)}</pre>\n`;
        }
        const cls = lang ? ` class="language-${escapeHtml(lang.split(/\s/)[0])}"` : '';
        return `<pre><code${cls}>${escapeHtml(text)}</code></pre>\n`;
      },
      table(token) {
        const cell = (c, tag) => {
          const html = this.parser.parseInline(c.tokens);
          const plain = stripTags(html).trim();
          const isNum = /^[−+-]?[\d.,]+\s*(%|ms|일|인일|개|건|MB|GB|KB|×|x)?$/.test(plain);
          const cls = tag === 'td' && plain.length > 0 && plain.length <= 18 ? statusClass(plain) : null;
          const body = cls ? chip(html, cls) : html;
          const align = c.align ? ` style="text-align:${c.align}"` : '';
          return `<${tag}${isNum ? ' class="num"' : ''}${align}>${body}</${tag}>`;
        };
        const head = `<tr>${token.header.map(c => cell(c, 'th')).join('')}</tr>`;
        const rows = token.rows.map(r => `<tr>${r.map(c => cell(c, 'td')).join('')}</tr>`).join('\n');
        return `<div class="table-scroll"><table>\n<thead>${head}</thead>\n<tbody>${rows}</tbody></table></div>\n`;
      },
      listitem(item) {
        let body = this.parser.parse(item.tokens, !!item.loose);
        if (!item.task) return `<li>${body}</li>\n`;
        body = body.replace(/^<input[^>]*>\s*/, '');
        return `<li class="task"><input type="checkbox" disabled${item.checked ? ' checked' : ''}> ${body}</li>\n`;
      },
      checkbox() { return ''; },
    },
  });

  const tokens = marked.lexer(source);

  // 첫 h1 이 제목, 그 뒤 첫 제목 전까지의 문단은 머리말(수립일·관련 문서 같은 메타)이다.
  let title = null;
  const preamble = [];
  const bodyTokens = [];
  let inPreamble = false;
  for (const t of tokens) {
    if (!title && t.type === 'heading' && t.depth === 1) { title = t; inPreamble = true; continue; }
    if (inPreamble && t.type === 'heading') inPreamble = false;
    (inPreamble && (t.type === 'paragraph' || t.type === 'space') ? preamble : bodyTokens).push(t);
    if (inPreamble && t.type !== 'paragraph' && t.type !== 'space') inPreamble = false;
  }
  // h1 이 없는 문서: 파일 이름을 제목으로 쓴다.
  const titleHtml = title ? marked.parser([{ ...title, depth: 1 }]).replace(/^<h1[^>]*>|<\/h1>\s*$/g, '') : escapeHtml(posix.basename(file, '.md'));
  const titleText = stripTags(titleHtml).trim();
  outline.length = 0;

  // h2 단위로 절을 나눈다. 첫 h2 전의 본문은 머리 절이다.
  const sections = [];
  let currentSection = { head: null, tokens: [] };
  for (const t of bodyTokens) {
    if (t.type === 'heading' && t.depth === 2) {
      sections.push(currentSection);
      currentSection = { head: t, tokens: [] };
    } else currentSection.tokens.push(t);
  }
  sections.push(currentSection);

  const preambleHtml = marked.parser(preamble.filter(t => t.type === 'paragraph'));
  const rendered = sections.filter(s => s.head || s.tokens.some(t => t.type !== 'space')).map((s, index) => {
    const bodyHtml = marked.parser(s.tokens);
    if (!s.head) return { html: `<section class="prose">${bodyHtml}</section>`, size: 0 };
    const headHtml = marked.parser([{ ...s.head, depth: 2 }]);
    const entry = outline.find(o => o.depth === 2 && headHtml.includes(`id="${escapeHtml(o.id)}"`));
    const number = (entry?.text.match(/^(\d+(?:\.\d+)*)[.)]?\s/) || [])[1];
    const label = number ? `§${number}` : `§${String(index).padStart(2, '0')}`;
    const chips = [...(headHtml + bodyHtml).matchAll(/<span class="chip (\w+)"/g)].map(m => m[1]);
    return {
      id: entry?.id, text: entry?.text ?? stripTags(headHtml), label, chips,
      size: stripTags(bodyHtml).length,
      html: `<section class="doc-sec" id="sec-${escapeHtml(entry?.id ?? index)}">
<div class="sec-head"><p class="ve-label">${label}</p>${headHtml.trim()}</div>
<div class="prose">${bodyHtml}</div>
</section>`,
    };
  });

  const named = rendered.filter(r => r.id);
  const tasks = [...source.matchAll(/^\s*[-*+] \[([ xX])\]/gm)];
  const tasksDone = tasks.filter(m => m[1] !== ' ').length;
  const allChips = named.flatMap(r => r.chips);

  return { titleHtml, titleText, preambleHtml, rendered, named, tasks: tasks.length, tasksDone, allChips, mermaidCount };
}

const STATUS_NAME = { ok: '완료·통과', info: '진행·부분', warn: '주의', risk: '차단·중단', dim: '대기·미검증' };

function docMapFigure(doc) {
  if (doc.named.length < 3) return '';
  const max = Math.max(...doc.named.map(r => r.size), 1);
  const rows = doc.named.map(r => {
    const width = Math.max(1.5, Math.round(r.size / max * 1000) / 10);
    const counts = Object.entries(r.chips.reduce((a, c) => (a[c] = (a[c] || 0) + 1, a), {}));
    const marks = counts.map(([cls, n]) => `<span class="chip ${cls}" title="${STATUS_NAME[cls]}">${n}</span>`).join(' ');
    return `<a class="map-row" href="#${escapeHtml(r.id)}"><span class="ve-label">${r.label}</span><span class="map-name">${escapeHtml(r.text.replace(/^\d+(?:\.\d+)*[.)]?\s+/, ''))}</span><span class="map-bar"><i style="width:${width}%"></i></span><span class="map-marks">${marks}</span></a>`;
  }).join('\n');
  const largest = doc.named.reduce((a, b) => (b.size > a.size ? b : a));
  return `<figure>
<div class="frame doc-map" role="img" aria-label="절 ${doc.named.length}개의 분량 막대">
${rows}
</div>
<figcaption><span class="fig-n">그림 1</span>절 ${doc.named.length}개 가운데 「${escapeHtml(largest.text)}」가 가장 길다. 막대는 글자 수, 칩은 표·제목에 적힌 상태 수다.</figcaption>
</figure>`;
}

function statusFigure(doc) {
  const blocks = [];
  if (doc.tasks > 0) {
    const pct = Math.round(doc.tasksDone / doc.tasks * 100);
    blocks.push(`<div class="kpi"><span class="ve-label">체크 항목 완료</span><span class="val">${pct}<span class="unit">%</span></span>
<div class="stack" role="img" aria-label="${doc.tasks}개 중 ${doc.tasksDone}개 완료"><i class="ok" style="flex:${doc.tasksDone}"></i><i class="dim" style="flex:${doc.tasks - doc.tasksDone}"></i></div>
<span class="ve-label">${doc.tasksDone} / ${doc.tasks}</span></div>`);
  }
  const counts = doc.allChips.reduce((a, c) => (a[c] = (a[c] || 0) + 1, a), {});
  const order = ['ok', 'info', 'warn', 'risk', 'dim'].filter(k => counts[k]);
  const total = order.reduce((s, k) => s + counts[k], 0);
  if (total >= 3) {
    blocks.push(`<div class="kpi"><span class="ve-label">표·제목에 적힌 상태</span><span class="val">${total}<span class="unit">곳</span></span>
<div class="stack" role="img" aria-label="${order.map(k => `${STATUS_NAME[k]} ${counts[k]}`).join(', ')}">${order.map(k => `<i class="${k}" style="flex:${counts[k]}"></i>`).join('')}</div>
<span class="legend">${order.map(k => `<span class="chip ${k}">${STATUS_NAME[k]} ${counts[k]}</span>`).join('')}</span></div>`);
  }
  return blocks.length ? `<div class="kpis">${blocks.join('\n')}</div>` : '';
}

const PAGE_CSS = `
  .doc-map { display: grid; gap: 2px; padding: var(--s-4); }
  .map-row { display: grid; grid-template-columns: 3.5rem minmax(0, 16rem) minmax(4rem, 1fr) auto; align-items: center; gap: var(--s-3); padding: var(--s-1) var(--s-2); border-radius: var(--r-1); color: var(--text); text-decoration: none; }
  .map-row:hover, .map-row:focus-visible { background: var(--accent-dim); color: var(--text); }
  .map-name { font-size: var(--t-small); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .map-bar { height: .5rem; background: var(--border); border-radius: 2px; overflow: hidden; }
  .map-bar i { display: block; height: 100%; background: var(--border-bright); }
  .map-row:hover .map-bar i { background: var(--accent); }
  .map-marks { display: flex; gap: var(--s-1); justify-content: flex-end; min-width: 3rem; }
  @media (max-width: 40rem) { .map-row { grid-template-columns: 3rem minmax(0, 1fr) 4rem; } .map-marks { display: none; } }`;

function page(doc, file, date) {
  const handoffDocument = file.startsWith('docs/handoff/');
  const depth = file.split('/').length - 2;
  const up = depth ? '../'.repeat(depth) : './';
  const toc = doc.named.length >= 4
    ? `<nav class="toc" aria-label="목차"><span class="ve-label">목차</span><ol>${doc.named.map(r => `<li><a href="#${escapeHtml(r.id)}">${escapeHtml(r.text)}</a></li>`).join('')}</ol></nav>`
    : '';
  const meta = doc.preambleHtml.trim() ? `<div class="meta">${doc.preambleHtml}</div>` : '';
  const md = posix.basename(file);
  return `<!DOCTYPE html>
<!-- ${GENERATOR} 가 ${file} 에서 만들었다. 이 파일을 고치지 말고 Markdown 을 고친 뒤 다시 실행한다. -->
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${escapeHtml(doc.titleText)}</title>
${FAVICON}
${FONT_LINKS}
<style>${TOKENS}${BASE_CSS}${PAGE_CSS}
${handoffDocument ? `
.human-handoff .meta{font-family:inherit;font-size:1rem;line-height:1.8;color:var(--text)}
.human-handoff .meta p{max-width:75ch}
.human-handoff h1{font-size:clamp(1.9rem,4vw,2.8rem);line-height:1.3}
` : ''}
</style>
</head>
<body${handoffDocument ? ' class="human-handoff"' : ''}>
<div class="shell${toc ? ' has-toc' : ''}">
${toc}
<main>
<header class="page">
<nav class="crumbs" aria-label="위치"><a href="${up}RefactoringPlanDashboard.html">계획 대시보드</a><span>${escapeHtml(posix.dirname(file))}/</span><a href="${escapeHtml(md)}">원본 Markdown</a></nav>
<p class="eyebrow">${categoryOf(file)}</p>
<h1>${doc.titleHtml}</h1>
${meta}
</header>
${statusFigure(doc)}
${handoffDocument ? '' : docMapFigure(doc)}
${doc.rendered.map(r => r.html).join('\n')}
<section class="sources" aria-label="출처">
<p class="ve-label">출처</p>
<p><a href="${escapeHtml(md)}"><code>${escapeHtml(file)}</code></a>${handoffDocument ? ' · 전달용 문서' : date ? ` · 마지막 커밋 ${date}` : ' · 아직 커밋 전'} · <code>${GENERATOR}</code> 로 생성</p>
</section>
</main>
</div>
<script>${BASE_SCRIPT}
</script>
${doc.mermaidCount ? MERMAID_SCRIPT : ''}
</body>
</html>
`;
}

function main() {
  const all = listMarkdown();
  const markdownSet = new Set(all);
  const args = process.argv.slice(2).map(a => toPosix(relative(ROOT, resolve(a))));
  const targets = args.length ? args : all;
  const dates = lastCommitDates();
  let written = 0;
  for (const file of targets) {
    // 전용 visual-explainer 도식 생성기가 소유하는 문서는 일반 템플릿으로 덮어쓰지 않는다.
    if (file === 'docs/design/DedicatedServerHandoff.md') { continue; }
    if (!markdownSet.has(file)) { console.error(`Markdown 목록에 없다: ${file}`); process.exitCode = 1; continue; }
    // 원본의 줄바꿈이 섞여 있어도 결과는 LF 로 만든다.
    const source = readFileSync(join(ROOT, file), 'utf8').replace(/^﻿/, '').replace(/\r\n?/g, '\n');
    const doc = renderDocument(source, file, markdownSet);
    const out = join(ROOT, file.replace(/\.md$/, '.html'));
    writeFileSync(out, page(doc, file, dates.get(file)));
    written++;
  }
  console.log(`HTML ${written}개를 썼다 (Markdown ${targets.length}개 중)`);
}

main();
