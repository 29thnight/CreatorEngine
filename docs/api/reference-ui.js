// Curated, commit-pinned reference. Data is rendered with text nodes, never innerHTML.
const article=document.getElementById('doc-content');
const navigation=document.getElementById('doc-navigation');
const search=document.getElementById('doc-search');
const sidebar=document.getElementById('docs-sidebar');
const toggle=document.getElementById('docs-toggle');
const toc=document.getElementById('toc-links');
const pagination=document.getElementById('doc-pagination');
const main=document.getElementById('main');
const node=(tag,cls,text)=>{const n=document.createElement(tag);if(cls)n.className=cls;if(text!==undefined)n.textContent=text;return n};
const slug=s=>s.replace(/[^a-zA-Z0-9_-]+/g,'-').replace(/-$/,'').toLowerCase();
const link=(id,anchor='')=>'#'+id+(anchor?'~'+anchor:'');
const narrow = matchMedia('(max-width:700px)');
function openSidebar(open){
  sidebar.classList.toggle('open',open);toggle.setAttribute('aria-expanded',String(open));
  main.inert=narrow.matches&&open;
  const otherMenu=document.getElementById('mobile-menu');
  if(open&&otherMenu){otherMenu.hidden=true;document.querySelector('.menu-toggle')?.setAttribute('aria-expanded','false')}
}
narrow.addEventListener('change',()=>{openSidebar(false);main.inert=false});
toggle.addEventListener('click',()=>openSidebar(!sidebar.classList.contains('open')));
document.addEventListener('click',e=>{if(!sidebar.contains(e.target)&&!toggle.contains(e.target))openSidebar(false)});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&sidebar.classList.contains('open')){openSidebar(false);toggle.focus()}});
document.querySelector('.skip').addEventListener('click',e=>{e.preventDefault();openSidebar(false);main.focus();main.scrollIntoView({block:'start'})});
const keywords=new Set('using public private protected internal static sealed abstract partial readonly class struct enum void virtual override bool int float string var new if return async await const explicit inline constexpr noexcept false true null get set in out where'.split(' '));
const types=new Set('Component Entity Transform NativeComponent Float2 Float3 Quaternion Input KeyCode SimulationScope Task CancellationToken Camera CameraComponent BlackBoard Animator EnhancedRenderGraph RGHandle RGTextureDesc RenderBackend'.split(' '));
function colorize(target,text){
  const re=/(\/\/[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b[A-Za-z_][A-Za-z_0-9]*\b|\b\d+(?:\.\d+)?[fFuU]?\b)/g;
  let cursor=0;
  for(const m of text.matchAll(re)){
    target.append(document.createTextNode(text.slice(cursor,m.index)));
    const t=m[0],cls=t.startsWith('//')?'syntax-comment':t.startsWith('"')||t.startsWith("'")?'syntax-string':keywords.has(t)?'syntax-key':types.has(t)?'syntax-type':/^\d/.test(t)?'syntax-num':'';
    target.append(cls?node('span',cls,t):document.createTextNode(t));cursor=m.index+t.length;
  }
  target.append(document.createTextNode(text.slice(cursor)));
}
function code(text,language='csharp'){
  const box=node('div','doc-code'),head=node('div','code-top');head.append(node('span','',language==='csharp'?'C#':language==='cpp'?'C++':language.toUpperCase()));
  const copy=node('button','copy-button','복사');copy.type='button';copy.setAttribute('aria-label','코드 복사');head.append(copy);
  const pre=node('pre'),content=node('code');colorize(content,text);pre.append(content);box.append(head,pre);
  copy.addEventListener('click',async()=>{
    try{if(!navigator.clipboard)throw Error('Clipboard unavailable');await navigator.clipboard.writeText(text);copy.textContent='복사됨';}
    catch{const range=document.createRange();range.selectNodeContents(content);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);copy.textContent='선택됨 · 직접 복사';}
    setTimeout(()=>{copy.textContent='복사'},2500);
  });return box;
}
async function start(){
  const response=await fetch('reference.json');if(!response.ok)throw Error('Reference HTTP '+response.status);
  const data=await response.json();if(data.meta?.schemaVersion!==1||!Array.isArray(data.pages))throw Error('Invalid reference schema');
  const pages=data.pages,byId=new Map(pages.map(p=>[p.id,p]));let current='',timer=0;
  const source=p=>'https://github.com/'+data.meta.repository+'/blob/'+data.meta.revision+'/'+p.source.split('/').map(encodeURIComponent).join('/');
  const corpus=new Map(pages.map(p=>[p.id,JSON.stringify([p.title,p.summary,p.members||[],p.sections]).normalize('NFKC').toLocaleLowerCase()]));
  function nav(){
    navigation.replaceChildren();const words=search.value.trim().normalize('NFKC').toLocaleLowerCase().split(/\s+/).filter(Boolean);
    const found=pages.filter(p=>words.every(w=>corpus.get(p.id).includes(w))),groups=new Map();
    for(const p of found){if(!groups.has(p.group))groups.set(p.group,[]);groups.get(p.group).push(p)}
    for(const [name,items] of groups){
      const group=node('section','nav-group');group.append(node('h2','',name));
      for(const p of items){const a=node('a');
        const matchesText=t=>words.every(w=>String(t).normalize('NFKC').toLocaleLowerCase().includes(w));
        const member=words.length&&!matchesText(p.title+' '+p.summary)?(p.members||[]).find(m=>matchesText(m.join(' '))):null;
        a.href=link(p.id,member?'member-'+slug(member[0]):'');a.dataset.page=p.id;if(p.id===current)a.setAttribute('aria-current','page');a.append(node('span','nav-kind',p.id.startsWith('script/')?'C#':p.id.startsWith('native/')?'C+':p.id.startsWith('tools/')?'>_':'↗'),document.createTextNode(p.id==='guide/overview'?'문서 개요':p.title.replace('\n',' ')));group.append(a)}navigation.append(group);
    }
    if(!found.length)navigation.append(node('p','empty-result','검색 결과가 없습니다. 타입 또는 메서드 이름으로 검색해 보세요.'));
    document.getElementById('search-status').textContent=`문서 ${found.length}개`;
  }
  function heading(id,title){const h=node('h2','',title);h.id=id;article.append(h);const a=node('a','',title);a.href=link(current,id);toc.append(a)}
  function render(){
    let hash;try{hash=decodeURIComponent(location.hash.slice(1))}catch{hash='invalid'}
    const [id,anchor]=hash?hash.split('~'):['guide/overview'];const p=byId.get(id),changed=current!==id;current=id;
    if(!p){article.replaceChildren(node('div','eyebrow','DOCUMENTATION'),node('h1','','문서를 찾을 수 없습니다.'),node('p','','주소에 해당하는 문서가 없습니다. 탐색 메뉴 또는 문서 개요로 이동하세요.'));const a=node('a','button secondary','문서 개요');a.href=link('guide/overview');article.append(a);toc.replaceChildren();pagination.replaceChildren();article.removeAttribute('data-ready');article.dataset.page=id;document.getElementById('source-link').hidden=true;openSidebar(false);main.focus({preventScroll:true});window.scrollTo({top:0,behavior:'instant'});document.title='문서를 찾을 수 없음 — Creator Engine';nav();return}
    if(changed||!article.dataset.ready){
      article.replaceChildren();toc.replaceChildren();article.dataset.ready='true';article.dataset.page=p.id;document.getElementById('source-link').hidden=false;document.title=p.title.replace('\n',' ')+' — Creator Engine';
      article.append(node('div','eyebrow','CREATOR ENGINE / '+p.group));const title=node('h1');p.title.split('\n').forEach((t,i)=>{if(i)title.append(node('br'));title.append(document.createTextNode(t))});article.append(title,node('p','doc-summary',p.summary));
      const tags=node('div','doc-tags');['Preview',p.id.startsWith('native/')?'Engine internals':p.id.startsWith('script/')?'CreatorEngine':'Guide','선별 레퍼런스'].forEach(t=>tags.append(node('span','',t)));article.append(tags);
      if(p.signature){heading('declaration','선언');article.append(code(p.signature,p.id.startsWith('native/')?'cpp':'csharp'))}
      if(p.note){heading('contract','사용 조건');const note=node('aside','callout');note.append(node('strong','','API 계약'),node('p','',p.note));article.append(note)}
      if(p.cards){const cards=node('div','doc-cards');p.cards.forEach(([title,description,id])=>{const a=node('a','doc-card');a.href=link(id);a.append(node('span','arrow','↗'),node('span','eyebrow','EXPLORE'),node('h3','',title),node('p','',description));cards.append(a)});article.append(cards)}
      for(const s of p.sections){heading(s.id,s.title);article.append(node('p','',s.text));if(s.code)article.append(code(s.code,s.language))}
      if(p.example){heading('example','사용 예제');article.append(code(p.example),node('p','page-index','현재 소스 계약에 맞춘 예제입니다. 네이티브 빌드·실행 검증과 구분합니다.'))}
      if(p.members){heading('members',`주요 멤버 (${p.members.length})`);article.append(node('p','page-index','시그니처는 접근 계약을 요약하며 표현식 본문·초깃값·일부 인라인 구현은 생략합니다. 전체 선언은 소스에서 확인하세요.'));for(const [name,signature,description,kind] of p.members){const box=node('section','member');box.id='member-'+slug(name);const h=node('h3','',name);h.append(node('span','member-tag',kind));box.append(h,code(signature,p.id.startsWith('native/')?'cpp':'csharp'),node('p','',description));const a=node('a','member-source','이 멤버 링크');a.href=link(p.id,box.id);box.append(a);article.append(box)}}
      const sourceNote=node('div','source-note');sourceNote.append(document.createTextNode('소스 기준 '+data.meta.revision.slice(0,7)+' · '));const a=node('a','',p.source);a.href=source(p);a.target='_blank';a.rel='noopener noreferrer';sourceNote.append(a);article.append(sourceNote);document.getElementById('source-link').href=source(p);
      pagination.replaceChildren();const index=pages.indexOf(p);for(const [j,label] of [[index-1,'PREVIOUS'],[index+1,'NEXT']]){if(j<0||j>=pages.length)continue;const a=node('a');a.href=link(pages[j].id);a.append(node('small','',label),document.createTextNode(pages[j].id==='guide/overview'?'문서 개요':pages[j].title.replace('\n',' ')));pagination.append(a)}
    }
    nav();openSidebar(false);requestAnimationFrame(()=>{if(anchor){const target=document.getElementById(anchor);if(target&&article.contains(target))target.scrollIntoView({block:'start',behavior:'instant'})}else if(changed){window.scrollTo({top:0,behavior:'instant'});main.focus({preventScroll:true})}});
  }
  search.addEventListener('input',()=>{clearTimeout(timer);timer=setTimeout(nav,70)});
  search.addEventListener('keydown',e=>{if(e.key==='Enter'){const a=navigation.querySelector('a');if(a){location.hash=a.getAttribute('href');openSidebar(false)}}if(e.key==='Escape'){search.value='';nav();search.blur()}});
  document.addEventListener('keydown',e=>{const typing=e.target.matches('input,textarea,[contenteditable="true"]');if((e.key==='/'&&!typing)||((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='k')){e.preventDefault();if(matchMedia('(max-width:700px)').matches)openSidebar(true);search.focus();search.select()}});
  navigation.addEventListener('click',e=>{if(e.target.closest('a'))openSidebar(false)});window.addEventListener('hashchange',render);render();
}
start().catch(error=>{console.error(error);article.replaceChildren(node('h1','','문서를 불러오지 못했습니다.'),node('p','doc-loading-error','reference.json을 읽을 수 없습니다. 로컬에서는 HTTP 서버로 실행한 뒤 다시 시도하세요.'));const retry=node('button','button secondary','다시 시도');retry.addEventListener('click',()=>location.reload());article.append(retry)});
