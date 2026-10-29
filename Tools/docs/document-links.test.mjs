import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { resolveDocumentLink, renderDocumentLink } from './document-links.mjs';

const page = 'docs/analysis/Report.md';
const context = {
  markdownSet: new Set(['docs/analysis/Other.md']),
  tracked: new Set(['Engine/SceneRuntime/Scene.cpp', 'Tools/build/stage-headless.ps1'])
};

test('normalizes repository-style documentation links inside the deployed root', () => {
  assert.equal(resolveDocumentLink('../../docs/analysis/Other.md#scope', page, context).href, 'Other.html#scope');
  assert.equal(resolveDocumentLink('../../docs/analysis/Other.html?mode=1#scope', page, context).href, 'Other.html?mode=1#scope');
  assert.equal(resolveDocumentLink('/analysis/Other.html', page, context).href, 'Other.html');
});

test('retains artifact locations as local observations, even when files exist', () => {
  for (const href of ['../../Artifacts/capture/result.json', '../../Build/Verification/run/result.json',
    '../../../FSharp_Tool_Pakage_for_CreatorEngine/artifacts/run/result.json',
    '../../../CreatorContainers/results/result.json', '../../../CreatorContainers/include/ce/queue.h']) {
    const link = resolveDocumentLink(href, page, context);
    assert.equal(link.kind, 'local-record');
    const html = renderDocumentLink(link, '검증 기록');
    assert.ok(html.includes('사이트 미포함'));
    assert.ok(html.includes(`<code>${link.path}</code>`));
    assert.ok(!html.includes('<a '));
  }
});

test('retains a local viewer query and selected timeline anchor', () => {
  const link = resolveDocumentLink('../../Artifacts/run/timeline.html?dataset=after-etw&detail=declare#gpuTimeline', page, context);
  assert.equal(link.path, 'Artifacts/run/timeline.html?dataset=after-etw&detail=declare#gpuTimeline');
  assert.ok(renderDocumentLink(link, 'GPU').includes('dataset=after-etw&amp;detail=declare#gpuTimeline'));
});

test('pins tracked source links and retains source line anchors', () => {
  const link = resolveDocumentLink('../../Engine/SceneRuntime/Scene.cpp#L12', page, context);
  assert.match(link.href, /^https:\/\/github.com\/29thnight\/CreatorEngine\/blob\/[0-9a-f]{40}\/Engine\/SceneRuntime\/Scene.cpp#L12$/);
  assert.ok(renderDocumentLink(link, '소스').includes('소스 열람 기준'));
});

test('Tools/build contains authored scripts rather than local build output', () => {
  assert.equal(resolveDocumentLink('../../Tools/build/stage-headless.ps1', page, context).kind, 'source');
});

test('sibling repository documentation uses its own fixed revision', () => {
  const link = resolveDocumentLink('../../../FSharp_Tool_Pakage_for_CreatorEngine/vscode/README.md', page, context);
  assert.match(link.href, /FSharp_Tool_Pakage_for_CreatorEngine\/blob\/3351cbc80d7216eb10110c4c8b30dce98aef1d1f\/vscode\/README.md$/);
});

test('preserves external URLs, fragments and real missing document failures', () => {
  for (const href of ['https://example.com/a?b=1#c', '//example.com/a', '#local', 'Missing.html']) {
    assert.equal(resolveDocumentLink(href, page, context).href, href);
  }
});

test('escapes link attributes and local paths without discarding the label markup', () => {
  assert.ok(renderDocumentLink({ kind: 'local-record', path: 'Artifacts/a<b>.json' }, '<b>기록</b>').includes('a&lt;b&gt;.json'));
  assert.ok(renderDocumentLink({ kind: 'link', href: '?a=1&b=2' }, '열기', '"quoted"').includes('&quot;quoted&quot;'));
});

test('the live inventory page uses the same portable routes for dynamic links', () => {
  const source = readFileSync(new URL('../../docs/analysis/EngineWiringAudit20261020.src.html', import.meta.url), 'utf8');
  const data = JSON.parse(readFileSync(new URL('../../docs/analysis/EngineWiringAudit20261020.json', import.meta.url), 'utf8'));
  const block = source.match(/\/\/ SITE_LINK_POLICY_BEGIN([\s\S]*?)\/\/ SITE_LINK_POLICY_END/);
  assert.ok(block, 'actual inventory link producer is registered');
  const href = vm.runInNewContext(block[1] + '; href', { data });
  assert.equal(href('docs/plans/EngineLayerSeparationPlan.md'), '../plans/EngineLayerSeparationPlan.html');
  assert.equal(href('Build/local/result.json'), null);
  assert.equal(href('Artifacts/local/result.json'), null);
  assert.ok(href('ScriptCore/SimulationScope.cs').includes('/' + data.baseline.head + '/'));
  for (const file of data.inventory.files) assert.match(href(file.path), /^https:\/\/github.com\/29thnight\/CreatorEngine\/blob\/[a-f0-9]{40}\//);
});
