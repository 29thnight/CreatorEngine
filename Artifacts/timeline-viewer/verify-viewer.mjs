import {chromium} from 'file:///C:/Users/lance/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright-core/index.mjs';
import {writeFile,readFile,stat} from 'node:fs/promises';
const root=new URL('./',import.meta.url);
const catalog=JSON.parse(await readFile(new URL('catalog.json',root),'utf8'));
const browser=await chromium.launch({channel:'msedge',headless:true});
try {
 const errors=[];const page=await browser.newPage({viewport:{width:1600,height:1000}});page.on('pageerror',e=>errors.push(String(e)));
 const address='http://127.0.0.1:8765/Artifacts/timeline-viewer/index.html';
 await page.goto(address+'?manifest=/Artifacts/gpu-marker-blob-20261022/timeline/viewer.json&dataset=on-a-etw&detail=all');
 await page.waitForFunction(()=>window.CETimelineReady&&window.CETimelineDataId==='on-a-etw',null,{timeout:120000});
 const newest=await page.evaluate(()=>({id:window.CETimelineDataId,identities:window.CERhiIdentityCount,collections:document.querySelector('#collection').options.length,overflow:document.documentElement.scrollWidth>innerWidth+1}));
 await page.screenshot({path:new URL('viewer-pc.png',root).pathname.slice(1)});
 await page.goto(address+'?manifest=/Artifacts/node-declare-parallel-20261021/timeline/viewer.json&dataset=after-etw&detail=all');
 await page.waitForFunction(()=>window.CETimelineReady&&window.CETimelineDataId==='after-etw',null,{timeout:120000});
 const legacy=await page.evaluate(()=>({id:window.CETimelineDataId,status:document.querySelector('#status').textContent}));
 await page.goto('http://127.0.0.1:8765/Tools/profiling-validation/viewer/index.html?manifest=/Artifacts/gpu-marker-blob-20261022/timeline/viewer.json&dataset=on-b-baseline');
 await page.waitForFunction(()=>window.CETimelineReady&&window.CETimelineDataId==='on-b-baseline',null,{timeout:120000});
 const redirect=page.url();
 if(errors.length||newest.overflow||!newest.identities||newest.collections!==catalog.measurements.length||!redirect.includes('/Artifacts/timeline-viewer/'))throw Error(JSON.stringify({errors,newest,legacy,redirect}));
 await writeFile(new URL('viewer-verification.json',root),JSON.stringify({passed:true,newest,legacy,redirect,errors},null,2));
}finally {await browser.close()}
