// Parse Markdown links as tokens, so examples and inline code are not links.
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { marked } from 'marked';
const result = {};
function visit(dir) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) { if (entry.name !== 'dist') visit(path); continue; }
    if (!entry.name.endsWith('.md')) continue;
    const links = [], html = [];
    marked.walkTokens(marked.lexer(readFileSync(path, 'utf8')), token => {
      if (token.type === 'link' || token.type === 'image') links.push(token.href);
      if (token.type === 'html') html.push(token.text);
    });
    result[relative('.', path).replaceAll('\\', '/')] = { links, html };
  }
}
visit('docs');
writeFileSync(process.argv[2] ?? 'Build/document-links.json', JSON.stringify(result, null, 2));
