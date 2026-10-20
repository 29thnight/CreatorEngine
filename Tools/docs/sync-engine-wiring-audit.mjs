import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import { spawnSync } from 'node:child_process';
const root = process.cwd();
const base = 'docs/analysis/EngineWiringAudit20261020';
const dataPath = path.resolve(root, base + '.json');
const srcPath = path.resolve(root, base + '.src.html');
let source = fs.readFileSync(srcPath, 'utf8');
const dataText = fs.readFileSync(dataPath, 'utf8');
const data = JSON.parse(dataText);
const rulesText = source.match(/<script id="audit-rules">([\s\S]*?)<\/script>/)?.[1];
if (!rulesText) throw new Error('Audit rules missing');
const context = vm.createContext({});
vm.runInContext(rulesText, context);
const problems = context.AuditRules.validate(data);
if (problems.length) throw new Error(problems.join('\n'));
for (const item of data.inventory.files) {
  if (!fs.existsSync(item.path)) throw new Error('Source missing: ' + item.path);
  const actual = crypto.createHash('sha256').update(fs.readFileSync(item.path)).digest('hex');
  if (actual !== item.sha256) throw new Error('Source changed; review affected audit evidence: ' + item.path);
}
const policy = data.inventory.policy;
const git = spawnSync('git', ['-c', 'core.quotepath=false', 'ls-files', '--cached', '--others', '--exclude-standard'],
  { cwd: root, encoding: 'utf8' });
if (git.status !== 0) throw new Error('Cannot enumerate current source inventory');
const known = new Set(data.inventory.files.map(f => f.path));
for (const file of new Set(git.stdout.trim().split(/\r?\n/))) {
  const parts = file.split('/'), extension = path.extname(file).toLowerCase();
  const source = (parts.length > 1 && policy.roots.includes(parts[0]) && policy.extensions.includes(extension)) ||
    (parts.length === 1 && (policy.rootExtensions.includes(extension) || policy.rootNames.includes(file))) ||
    (policy.configExtensions.includes(extension) && policy.configPrefixes.some(prefix => file.startsWith(prefix))) ||
    (parts.length > 1 && policy.roots.includes(parts[0]) && ['packages.config', 'CMakeLists.txt'].includes(path.basename(file)));
  if (source && !known.has(file) && fs.existsSync(file)) throw new Error('New source needs routing and audit review: ' + file);
}
const hash = crypto.createHash('sha256').update(dataText).digest('hex');
const tag = '<script id="audit-data" type="application/json" data-sha256="' + hash + '">' +
  JSON.stringify(data).replaceAll('<', '\\u003c') + '</script>';
const pattern = /<script id="audit-data" type="application\/json"[^>]*>[\s\S]*?<\/script>/;
if (!pattern.test(source)) throw new Error('Embedded audit data missing');
if (process.argv.includes('--check')) {
  if (source.match(pattern)[0] !== tag) throw new Error('JSON and HTML source differ; run sync without --check');
} else {
  source = source.replace(pattern, () => tag);
  fs.writeFileSync(srcPath, source, 'utf8');
}
const counts = {};
for (const area of data.areas) for (const unit of area.units) {
  const state = context.AuditRules.state(unit, data.criteria);
  counts[state] = (counts[state] || 0) + 1;
}
console.log(JSON.stringify({ areas: data.areas.length, units: data.areas.flatMap(a => a.units).length,
  inventory: data.inventory.files.length, states: counts, embeddedDataHash: hash }));
if (process.argv.includes('--render')) {
  const arg = process.argv.indexOf('--kit');
  const kit = arg >= 0 ? process.argv[arg + 1] : 'Build/visual-explainer-reference/plugins/visual-explainer/kit/render.mjs';
  if (!kit || !fs.existsSync(kit)) throw new Error('Set --kit to visual-explainer kit/render.mjs');
  const result = spawnSync(process.execPath, [kit, srcPath, '-o', path.resolve(root, base + '.html'), '--root', root],
    { stdio: 'inherit' });
  process.exit(result.status ?? 1);
}
