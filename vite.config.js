import { cpSync, readdirSync } from 'node:fs';
import { relative, resolve } from 'node:path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const docsRoot = resolve(__dirname, 'docs');

/* Vite가 직접 만들어내는 산출물. 나머지 docs 파일은 기존 주소 그대로 배포본에 남겨야 한다.
   기존 엔지니어링 문서와 대시보드를 배포에서 빠뜨리면 살아 있던 URL이 404가 된다. */
const generated = new Set(['dist', 'src', 'node_modules', 'index.html']);
const generatedInside = 'api/index.html';

export default defineConfig({
  root: docsRoot,
  base: './',
  plugins: [
    react(),
    {
      name: 'copy-doc-assets',
      closeBundle() {
        /* cpSync은 대상이 원본의 하위일 때 통째 복사를 거부한다. 최상위 항목을 하나씩 옮긴다. */
        const dist = resolve(docsRoot, 'dist');
        for (const entry of readdirSync(docsRoot)) {
          if (generated.has(entry)) continue;
          cpSync(resolve(docsRoot, entry), resolve(dist, entry), {
            recursive: true,
            force: false,
            filter: (source) => relative(docsRoot, source).replace(/\\/g, '/') !== generatedInside
          });
        }
      }
    }
  ],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        landing: resolve(docsRoot, 'index.html'),
        api: resolve(docsRoot, 'api/index.html')
      }
    }
  }
});
