const toggle=document.querySelector('.menu-toggle');
const menu=document.getElementById('mobile-menu');
function closeMenu(){if(!toggle||!menu)return;menu.hidden=true;toggle.setAttribute('aria-expanded','false');toggle.setAttribute('aria-label','메뉴 열기')}
if(toggle&&menu){toggle.addEventListener('click',()=>{menu.hidden=!menu.hidden;toggle.setAttribute('aria-expanded',String(!menu.hidden));toggle.setAttribute('aria-label',menu.hidden?'메뉴 열기':'메뉴 닫기')});menu.addEventListener('click',e=>{if(e.target.closest('a'))closeMenu()});document.addEventListener('click',e=>{if(!e.target.closest('.topbar'))closeMenu()});document.addEventListener('keydown',e=>{if(e.key==='Escape'){const wasOpen=!menu.hidden;closeMenu();if(wasOpen)toggle.focus()}})}
const captures=['resource/CE_260914.png','resource/스크린샷 2026-09-14 101942.png','resource/스크린샷 2026-09-14 131741.png'];
const image=document.getElementById('capture');
const link=document.getElementById('capture-link');
if(image&&link){
  let index=0,returnFocus=null;
  function imageState(){link.classList.toggle('is-loading',!image.complete);link.classList.toggle('is-error',image.complete&&image.naturalWidth===0)}
  image.addEventListener('load',imageState);image.addEventListener('error',imageState);imageState();
  const dialog=document.getElementById('image-dialog');
  const enlarged=document.getElementById('dialog-image');
  document.querySelectorAll('[data-capture]').forEach(button=>button.addEventListener('click',()=>{
    index=Number(button.dataset.capture);if(!Number.isInteger(index)||index<0||index>=captures.length)return;
    link.classList.add('is-loading');link.classList.remove('is-error');image.src=captures[index];image.alt=`Creator Engine 에디터의 2026년 9월 14일 캡처 ${index+1}`;link.href=captures[index];link.setAttribute('aria-label',`에디터 스크린샷 ${index+1} 원본 확대`);
    document.querySelectorAll('[data-capture]').forEach(b=>{const selected=Number(b.dataset.capture)===index;b.classList.toggle('selected',selected);b.setAttribute('aria-pressed',String(selected))});
    document.querySelector('.gallery-count').textContent=`0${index+1} / 03`;
  }));
  link.addEventListener('click',e=>{
    // Preserve ordinary link semantics: new tabs still open the original capture.
    if(e.metaKey||e.ctrlKey||e.shiftKey||e.altKey||e.button!==0||!dialog?.showModal)return;
    e.preventDefault();returnFocus=document.activeElement;enlarged.src=captures[index];enlarged.alt=image.alt;dialog.showModal();
  });
  dialog?.querySelector('.dialog-close')?.addEventListener('click',()=>dialog.close());
  dialog?.addEventListener('click',e=>{if(e.target===dialog){const r=dialog.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)dialog.close()}});
  dialog?.addEventListener('close',()=>{enlarged.removeAttribute('src');returnFocus?.focus()});
}
const scrollSections=[...document.querySelectorAll('.brand-page main>section')];
if(scrollSections.length){
  document.body.classList.add('scroll-morph-ready');
  document.body.classList.add('step-scroll');
  const reduced=matchMedia('(prefers-reduced-motion: reduce)');
  let frame=0,lastScroll=window.scrollY;
  let activeSection=0,transitioning=false;
  function clamp(value,min=0,max=1){return Math.min(max,Math.max(min,value))}
  function sectionIndex(){
    const center=window.scrollY+window.innerHeight*.5;
    let nearest=0,distance=Infinity;
    scrollSections.forEach((section,index)=>{
      const next=Math.abs(section.offsetTop+section.offsetHeight*.5-center);
      if(next<distance){distance=next;nearest=index}
    });
    return nearest;
  }
  function goToSection(index){
    if(transitioning||index<0||index>=scrollSections.length)return;
    activeSection=index;transitioning=true;
    scrollSections[index].scrollIntoView({behavior:'smooth',block:'start'});
    window.setTimeout(()=>{transitioning=false;activeSection=sectionIndex()},850);
  }
  function stepWheel(event){
    if(reduced.matches||Math.abs(event.deltaY)<8)return;
    event.preventDefault();
    if(transitioning)return;
    const direction=event.deltaY>0?1:-1;
    goToSection(activeSection+direction);
  }
  document.addEventListener('wheel',stepWheel,{passive:false});
  window.addEventListener('keydown',event=>{
    if(reduced.matches||transitioning)return;
    const direction=event.key==='PageDown'||event.key==='ArrowDown'||event.key===' '?1:event.key==='PageUp'||event.key==='ArrowUp'?-1:0;
    if(!direction)return;
    event.preventDefault();
    goToSection(activeSection+direction);
  });
  function updateScrollMorph(){
    frame=0;
    const viewport=window.innerHeight;
    let strongest=null,strongestFocus=-1;
    for(const section of scrollSections){
      const rect=section.getBoundingClientRect();
      const center=rect.top+rect.height*.5;
      const distance=(center-viewport*.5)/(viewport*.78);
      const focus=clamp(1-Math.abs(distance));
      const direction=window.scrollY>=lastScroll?1:-1;
      section.style.setProperty('--section-focus',focus.toFixed(3));
      section.style.setProperty('--section-shift',((distance*direction*-28)).toFixed(2));
      section.classList.toggle('is-focused',focus>.72);
      if(focus>strongestFocus){strongest=section;strongestFocus=focus}
    }
    if(strongest) strongest.dataset.scrollFocus='true';
    lastScroll=window.scrollY;
    activeSection=sectionIndex();
  }
  function requestScrollMorph(){if(!frame)frame=requestAnimationFrame(updateScrollMorph)}
  window.addEventListener('scroll',requestScrollMorph,{passive:true});
  window.addEventListener('resize',requestScrollMorph,{passive:true});
  reduced.addEventListener('change',()=>{document.body.classList.toggle('step-scroll',!reduced.matches);requestScrollMorph()});
  requestScrollMorph();
}
