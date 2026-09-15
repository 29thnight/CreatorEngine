// Original Canvas 2D lighting study. No React Bits or third-party animation code.
// Facets stay fixed; a moving point light changes their diffuse/specular response.
const canvas = document.getElementById('facets');
if (canvas) {
  const ctx = canvas.getContext('2d', { alpha: true });
  const hero = canvas.closest('.hero');
  const button = document.getElementById('motion-toggle');
  if (!ctx || !hero || !button) { if (button) button.hidden = true; }
  else {
    const reduced = matchMedia('(prefers-reduced-motion: reduce)');
    // Normalized 3D positions are authored to frame the quiet center of Loading.bmp.
    const points = [
      [-.12,-.12,.14],[.08,-.08,.3],[.19,.23,.72],[-.05,.03,-.08],
      [-.1,.39,.26],[.13,.46,.12],[-.06,.82,.18],[.04,1.03,-.22],
      [.25,-.12,.32],[.24,.1,-.05],[.31,-.03,.18],[.83,-.15,.32],
      [.78,.19,.76],[1.1,-.07,.13],[1.08,.32,.28],[.92,.47,.46],
      [1.12,.76,.13],[.89,.87,-.1],[.75,.73,.14],[1.09,1.1,.09],
      [.74,1.07,-.19],[.9,.07,.12],[.03,.11,.16]
    ];
    const triangles = [[0,1,2],[0,2,22],[22,2,3],[3,2,4],[4,2,5],[4,5,6],[6,5,7],[1,8,2],[8,9,2],[8,10,9],[10,11,12],[11,21,12],[21,13,12],[13,14,12],[14,15,12],[14,16,15],[16,17,15],[15,17,18],[17,19,18],[19,20,18]];
    const seams = [[0,2],[1,2],[2,5],[5,7],[10,12],[11,12],[12,15],[15,18],[18,20]];
    const normal = v => {const n = Math.hypot(...v)||1;return v.map(x=>x/n)};
    const faces = triangles.map(ids => {
      const p = ids.map(i=>points[i]);
      const a = p[1].map((x,i)=>x-p[0][i]),b=p[2].map((x,i)=>x-p[0][i]);
      let n=normal([a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]);
      if(n[2]<0)n=n.map(x=>-x);
      return {p,n,c:[0,1,2].map(k=>p.reduce((s,q)=>s+q[k],0)/3)};
    });
    let width=0,height=0,raf=0,previous=0,clock=0,visible=true,paused=false,disposed=false;
    const pointer={x:.5,y:.3,weight:0};
    let light={x:.72,y:.13};
    function path(p){ctx.beginPath();p.forEach((v,i)=>i?ctx.lineTo(v[0]*width,v[1]*height):ctx.moveTo(v[0]*width,v[1]*height));ctx.closePath()}
    function paint(t, advance=true){
      ctx.clearRect(0,0,width,height);
      const drift={x:.53+Math.sin(t*.15)*.33,y:.24+Math.cos(t*.19)*.12};
      const w=reduced.matches?0:pointer.weight;
      if(advance){light.x+=(drift.x*(1-w)+pointer.x*w-light.x)*.055;light.y+=(drift.y*(1-w)+pointer.y*w-light.y)*.055}
      const l=[light.x,light.y,1.2];
      for (let index=0;index<faces.length;index++){
        const f=faces[index],d=l.map((v,i)=>v-f.c[i]),length=Math.hypot(...d),dir=normal(d);
        const diffuse=Math.max(0,f.n.reduce((s,v,i)=>s+v*dir[i],0));
        const half=normal([dir[0],dir[1],dir[2]+1]);
        const spec=Math.pow(Math.max(0,f.n.reduce((s,v,i)=>s+v*half[i],0)),22);
        const near=Math.exp(-Math.hypot((f.c[0]-l[0])*1.3,f.c[1]-l[1])*2.9);
        const energy=(diffuse*.52+spec*.42)/(1+length)*(.25+near*1.8);
        const shade=(index%4)*.016;
        path(f.p);
        const g=ctx.createLinearGradient(f.p[0][0]*width,f.p[0][1]*height,f.p[2][0]*width,f.p[2][1]*height);
        g.addColorStop(0,`rgb(${7+energy*32} ${14+energy*92} ${24+energy*157})`);
        g.addColorStop(1,`rgb(${4+shade*55} ${8+energy*33} ${14+energy*62})`);
        ctx.fillStyle=g;ctx.fill();ctx.strokeStyle=`rgba(111,182,232,${.035+energy*.19})`;ctx.lineWidth=.65;ctx.stroke();
      }
      for(const [ai,bi] of seams){
        const a=points[ai],b=points[bi];
        const dist=Math.hypot((a[0]+b[0])*.5-light.x,(a[1]+b[1])*.5-light.y);
        const e=.12+Math.exp(-dist*2.2)*.85;
        const g=ctx.createLinearGradient(a[0]*width,a[1]*height,b[0]*width,b[1]*height);
        g.addColorStop(0,`rgba(27,138,225,${e*.12})`);g.addColorStop(.65,`rgba(70,184,255,${e})`);g.addColorStop(1,`rgba(154,225,255,${e*.75})`);
        ctx.beginPath();ctx.moveTo(a[0]*width,a[1]*height);ctx.lineTo(b[0]*width,b[1]*height);
        ctx.strokeStyle=g;ctx.lineWidth=1.2;ctx.shadowColor='#168dff';ctx.shadowBlur=13;ctx.stroke();ctx.shadowBlur=0;
      }
      const glow=ctx.createRadialGradient(light.x*width,light.y*height,0,light.x*width,light.y*height,width*.4);
      glow.addColorStop(0,'rgba(22,109,190,.08)');glow.addColorStop(1,'rgba(10,30,60,0)');ctx.fillStyle=glow;ctx.fillRect(0,0,width,height);
    }
    const active=()=>!disposed&&!paused&&!reduced.matches&&visible&&!document.hidden;
    function frame(now){raf=0;if(!active())return;if(!previous)previous=now;const dt=now-previous;if(dt>=1000/30){clock+=Math.min(dt,100)/1000;previous=now;paint(clock)}raf=requestAnimationFrame(frame)}
    function schedule(){if(raf){cancelAnimationFrame(raf);raf=0}previous=0;if(active())raf=requestAnimationFrame(frame)}
    function resize(){const r=hero.getBoundingClientRect();width=r.width;height=r.height;const dpr=Math.min(devicePixelRatio||1,1.5);canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);paint(clock,active());schedule()}
    function updateButton(){const off=paused||reduced.matches;button.setAttribute('aria-pressed',String(off));button.disabled=reduced.matches;button.textContent=reduced.matches?'모션 감소 설정 적용됨':paused?'조명 애니메이션 재생 ▷':'조명 애니메이션 일시정지 Ⅱ'}
    hero.addEventListener('pointermove',e=>{if(e.pointerType==='touch'||!active())return;const r=hero.getBoundingClientRect();pointer.x=(e.clientX-r.left)/r.width;pointer.y=(e.clientY-r.top)/r.height;pointer.weight=.7},{passive:true});
    hero.addEventListener('pointerleave',()=>{pointer.weight=0},{passive:true});
    button.addEventListener('click',()=>{paused=!paused;updateButton();schedule()});
    document.addEventListener('visibilitychange',schedule);
    reduced.addEventListener('change',()=>{updateButton();paint(clock,active());schedule()});
    const io=new IntersectionObserver(entries=>{visible=entries[0].isIntersecting;schedule()});io.observe(hero);
    const ro=new ResizeObserver(resize);ro.observe(hero);
    window.addEventListener('pagehide',()=>{disposed=true;schedule();io.disconnect();ro.disconnect()});
    window.addEventListener('pageshow',e=>{if(e.persisted){disposed=false;io.observe(hero);ro.observe(hero);resize()}});
    hero.dataset.motionReady='true';updateButton();resize();
  }
}
