import { readFileSync } from 'node:fs';
import { posix } from 'node:path';

const policy = JSON.parse(readFileSync(new URL('./document-link-policy.json', import.meta.url), 'utf8'));
const escape = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');

// A report path is an observation, not a downloadable website asset. Never
// copy a local capture into the published site to make its link appear valid.
export function resolveDocumentLink(href, document, { markdownSet = new Set(), tracked = new Set() } = {}) {
  if (!href || /^(?:[a-z][a-z\d+.-]*:|#|\/\/)/i.test(href)) return { kind: 'link', href };

  const match = href.match(/^([^?#]*)([?#].*)?$/);
  const suffix = match[2] || '';
  const target = posix.normalize(match[1].startsWith('/')
    ? posix.join('docs', decodeURI(match[1]))
    : posix.join(posix.dirname(document), decodeURI(match[1])));
  const parts = target.split('/');

  if (/^(?:artifacts|build|bin)$/i.test(parts[0]) ||
      (target.startsWith('../FSharp_Tool_Pakage_for_CreatorEngine/') && parts.some(part => /^artifacts$/i.test(part)))) {
    return { kind: 'local-record', path: target + suffix };
  }

  if (target.startsWith('docs/')) {
    const rendered = target.endsWith('.md') && markdownSet.has(target) ? target.slice(0, -3) + '.html' : target;
    return { kind: 'link', href: posix.relative(posix.dirname(document), rendered) + suffix };
  }

  const sibling = '../FSharp_Tool_Pakage_for_CreatorEngine/';
  if (target.startsWith(sibling)) {
    return { kind: 'source', href: `${policy.fsharpRepository}/blob/${policy.fsharpRevision}/${target.slice(sibling.length)}${suffix}`, revision: policy.fsharpRevision };
  }

  if (tracked.has(target)) {
    return { kind: 'source', href: `${policy.engineRepository}/blob/${policy.engineRevision}/${target}${suffix}`, revision: policy.engineRevision };
  }

  // Keep genuine unknown links visible to the package verifier; do not turn
  // arbitrary missing documents into successful-looking placeholders.
  return { kind: 'link', href };
}

export function renderDocumentLink(link, label, title = '') {
  if (link.kind === 'local-record') {
    return `<span class="local-record">${label} <small>로컬 기록 · 사이트 미포함</small> (<code>${escape(link.path)}</code>)</span>`;
  }

  const sourceTitle = link.kind === 'source'
    ? `소스 열람 기준: ${link.revision}. 측정 당시 소스 신원은 문서의 원본 기록 참조.`
    : '';
  const description = [title, sourceTitle].filter(Boolean).join(' · ');
  return `<a href="${escape(link.href)}"${description ? ` title="${escape(description)}"` : ''}>${label}</a>`;
}
