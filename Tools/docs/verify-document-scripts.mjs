import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
const failures=[];let pages=0,scripts=0;
function visit(dir){
  for(const entry of readdirSync(dir,{withFileTypes:true})){
    const file=join(dir,entry.name);
    if(entry.isDirectory()){if(entry.name!=='dist')visit(file);continue;}
    if(!entry.name.endsWith('.html'))continue;
    pages++;
    for(const match of readFileSync(file,'utf8').matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/gi)){
      const attrs=match[1];
      const type=attrs.match(/\btype\s*=\s*["']([^"']*)/i)?.[1];
      if(/\bsrc\s*=/.test(attrs)||(type&&!['text/javascript','application/javascript'].includes(type)))continue;
      try{new Function(match[2]);scripts++;}catch(error){failures.push({file,error:String(error)});}
    }
  }
}
visit('docs');
writeFileSync('Build/full-docs-script-verification.json',JSON.stringify({pages,scripts,failures},null,2));
console.log(JSON.stringify({pages,scripts,failures:failures.length}));
if(failures.length)process.exitCode=1;
