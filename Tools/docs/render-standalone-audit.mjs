import { readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
const data=JSON.parse(readFileSync('docs/analysis/MasterDocumentationAudit20261020.json','utf8'));
const results=[];
const hash=file=>createHash('sha256').update(readFileSync(file)).digest('hex');
for(const doc of data.standalone.filter(d=>d.source.endsWith('.src.html'))){
  let passed=false;
  for(let attempt=1;attempt<=3;attempt++){
    try{
      process.stdout.write(execFileSync(process.execPath,['Build/visual-explainer-reference/plugins/visual-explainer/kit/render.mjs',doc.source,'--root',process.cwd()],{encoding:'utf8',maxBuffer:1<<24}));
      passed=true;break;
    }catch(error){
      const output=String(error.stdout??'')+String(error.stderr??'');
      if(attempt===3||!/ENOENT|EPERM|kit's script did not run/.test(output)){process.stdout.write(output);break;}
    }
  }
  results.push({file:doc.source,html:doc.path,passed,visualChecked:passed,sourceSha256:hash(doc.source),htmlSha256:hash(doc.path)});
}
writeFileSync('Build/full-docs-standalone-visual-final.json',JSON.stringify({master:data.master,results},null,2)+'\n');
if(results.some(d=>!d.passed))process.exitCode=1;
