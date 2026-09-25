import { useEffect, useMemo, useRef, useState } from 'react';

const captures = [
  'resource/CE_260914.png',
  'resource/스크린샷 2026-09-14 101942.png',
  'resource/스크린샷 2026-09-14 131741.png'
];

const isApiPage = () => location.pathname.replace(/\\/g, '/').includes('/api/');
const asset = (path) => (isApiPage() ? `../${path}` : path);
const apiLink = (hash) => (isApiPage() ? `#${hash}` : `api/#${hash}`);
const dashboardLink = (api) => (api ? '../RefactoringPlanDashboard.html' : 'RefactoringPlanDashboard.html');

function Header({ api = false }) {
  const [open, setOpen] = useState(false);
  const close = () => setOpen(false);
  return <header className="topbar">
    <a className="wordmark" href={api ? '../' : './'} aria-label="Creator Engine 홈"><img src={asset('site-assets/mark.svg')} width="32" height="32" alt="" /><span>Creator Engine</span></a>
    <nav className="desktop-nav" aria-label="주 메뉴">
      <a href={api ? '../' : './'} aria-current={!api ? 'page' : undefined}>엔진</a>
      <a href={api ? './' : 'api/'} aria-current={api ? 'page' : undefined}>문서 · API</a>
      <a href={dashboardLink(api)}>대시보드</a>
      <a href="https://github.com/29thnight/CreatorEngine" target="_blank" rel="noopener noreferrer">GitHub <span aria-hidden="true">↗</span></a>
    </nav>
    <div className="nav-end"><span className="preview-badge"><i /> Preview</span><button className="menu-toggle icon-button" aria-label={open ? '메뉴 닫기' : '메뉴 열기'} aria-controls="mobile-menu" aria-expanded={open} onClick={() => setOpen(!open)}>☰</button></div>
    <nav id="mobile-menu" className="mobile-menu" aria-label="모바일 메뉴" hidden={!open}>
      <a href={api ? '../' : './'} onClick={close}>엔진</a><a href={api ? './' : 'api/'} onClick={close}>문서 · API</a><a href={dashboardLink(api)} onClick={close}>대시보드</a><a href="https://github.com/29thnight/CreatorEngine" target="_blank" rel="noopener noreferrer">GitHub ↗</a>
    </nav>
  </header>;
}

/* Loading.bmp의 배경 구성을 따른다. 검은 바탕 위로 화면 가장자리에서 안쪽을 향해 뻗는 저폴리 면,
   그리고 소수의 얇은 청백색 균열. 화면 중앙과 아래쪽은 본문 가독성을 위해 검정으로 남긴다. */
const FACET_ANCHORS = [
  /* 원점 x, 원점 y, 시작 각, 끝 각, 부채살 수, 최소 길이, 최대 길이, 고리 수, 세로 비율 */
  [-.06, -.08, .05, 1.50, 7, .34, .74, 2, 1.15],
  [1.06, -.08, 1.64, 3.10, 7, .34, .78, 2, 1.15],
  [.32, -.10, .30, 2.85, 5, .16, .40, 1, 1.30],
  [-.07, .42, -.70, .70, 5, .18, .40, 1, 1.05],
  [1.07, .38, 2.46, 3.84, 5, .20, .44, 1, 1.05],
  [-.06, 1.08, -1.52, -.06, 4, .26, .52, 1, .95],
  [1.06, 1.08, 3.22, 4.70, 4, .26, .54, 1, .95]
];
const FACET_CRACKS = [
  /* 시작점, 끝점, 두께, 위상 */
  [[-.02, -.06], [.30, .52], 1.9, 0],
  [[.10, -.08], [.44, .34], 1.2, 1.7],
  [[1.02, -.06], [.62, .62], 1.9, 2.6],
  [[.98, .10], [.82, .74], 1.4, 4.1],
  [[.86, .16], [.90, .58], 1.1, 5.2]
];
const FACET_FPS = 30;
const CRACK_SPREAD = .26;

const seeded = (seed) => () => (seed = (seed * 1664525 + 1013904223) % 4294967296) / 4294967296;
const smoothstep = (from, to, value) => { const t = Math.min(1, Math.max(0, (value - from) / (to - from))); return t * t * (3 - 2 * t); };
/* 면이 남는 자리. 위쪽과 좌우 가장자리는 살리고 중앙·하단은 0으로 지운다. */
const facetMask = (x, y) => Math.max(
  (1 - smoothstep(.02, .72, y)) * (.1 + .9 * smoothstep(.04, .3, Math.abs(x - .5))),
  smoothstep(.74, 1.12, y) * smoothstep(.14, .46, Math.abs(x - .5)) * .5
);

function buildFacets() {
  const facets = [];
  FACET_ANCHORS.forEach(([ox, oy, from, to, rays, minReach, maxReach, rings, stretch], anchor) => {
    const random = seeded(9973 + anchor * 7919);
    const rows = [];
    for (let ring = 0; ring <= rings; ring += 1) {
      const row = [];
      for (let ray = 0; ray <= rays; ray += 1) {
        const angle = from + (to - from) * (ray / rays) + (random() - .5) * .2;
        const reach = (minReach + (maxReach - minReach) * (.25 + .75 * random())) * (.38 + .82 * ring);
        row.push([ox + Math.cos(angle) * reach, oy + Math.sin(angle) * reach * stretch]);
      }
      rows.push(row);
    }
    for (let ray = 0; ray < rays; ray += 1) facets.push([[ox, oy], rows[0][ray], rows[0][ray + 1]]);
    for (let ring = 0; ring < rings; ring += 1) {
      for (let ray = 0; ray < rays; ray += 1) {
        facets.push([rows[ring][ray], rows[ring][ray + 1], rows[ring + 1][ray]]);
        facets.push([rows[ring][ray + 1], rows[ring + 1][ray + 1], rows[ring + 1][ray]]);
      }
    }
  });
  /* 면마다 고정된 색조 편차와 마스크를 미리 굽는다. 프레임마다 다시 계산하지 않는다. */
  const tone = seeded(4242);
  return facets.map(([p, q, r]) => {
    const cx = (p[0] + q[0] + r[0]) / 3;
    const cy = (p[1] + q[1] + r[1]) / 3;
    const ux = q[0] - p[0], uy = q[1] - p[1];
    const vx = r[0] - p[0], vy = r[1] - p[1];
    const length = Math.hypot(ux + vx, uy + vy) || 1;
    return {
      points: [p, q, r],
      cx, cy,
      nx: (ux + vx) / length,
      ny: (uy + vy) / length,
      back: ux * vy - uy * vx < 0 ? .5 : 1,
      jitter: .4 + .6 * tone(),
      mask: facetMask(cx, cy)
    };
  }).filter((facet) => facet.mask >= .02);
}

function Facets() {
  const canvas = useRef(null);
  const hero = useRef(null);
  const [paused, setPaused] = useState(false);
  useEffect(() => {
    const element = canvas.current;
    const host = hero.current;
    const context = element?.getContext('2d');
    if (!context || !host) return undefined;
    const facets = buildFacets();
    const reduced = matchMedia('(prefers-reduced-motion: reduce)');
    let frame = 0, visible = true, width = 0, height = 0, previous = -Infinity;
    const draw = (time) => {
      context.setTransform(ratio(), 0, 0, ratio(), 0, 0);
      context.fillStyle = '#000000';
      context.fillRect(0, 0, width, height);
      const lightX = .26 + Math.sin(time * .00011) * .3;
      const lightY = .1 + Math.cos(time * .00014) * .12;
      context.lineJoin = 'bevel';
      for (const facet of facets) {
        const dx = lightX - facet.cx, dy = lightY - facet.cy;
        const distance = Math.hypot(dx, dy) || 1;
        const lambert = Math.max(0, (facet.nx * dx + facet.ny * dy) / distance);
        const falloff = Math.max(0, 1 - Math.hypot(dx, dy * .75) * .8) ** 1.5;
        const energy = ((.1 + .9 * lambert) * falloff + .1) * facet.jitter * facet.mask * facet.back;
        context.beginPath();
        facet.points.forEach(([x, y], index) => index ? context.lineTo(x * width, y * height) : context.moveTo(x * width, y * height));
        context.closePath();
        context.fillStyle = `rgb(${1 + energy * 30} ${3 + energy * 125} ${7 + energy * 208})`;
        context.fill();
        /* 원본처럼 빛을 정면으로 받은 면만 테두리를 얻는다. 모든 모서리를 빛내지 않는다. */
        if (energy > .3) {
          const lit = Math.min(1, (energy - .3) / .3);
          context.strokeStyle = `rgb(${40 + lit * 165} ${130 + lit * 115} ${175 + lit * 80})`;
          context.lineWidth = .7 + lit * .5;
          context.stroke();
        }
      }
      context.globalCompositeOperation = 'lighter';
      for (const [from, to, weight, phase] of FACET_CRACKS) {
        const slide = .42 + Math.sin(time * .00012 + phase) * .26;
        const gradient = context.createLinearGradient(from[0] * width, from[1] * height, to[0] * width, to[1] * height);
        gradient.addColorStop(0, 'rgba(118,203,255,0)');
        gradient.addColorStop(Math.max(.001, slide - CRACK_SPREAD), 'rgba(118,203,255,0)');
        gradient.addColorStop(slide, 'rgba(214,247,255,.94)');
        gradient.addColorStop(Math.min(.999, slide + CRACK_SPREAD), 'rgba(118,203,255,0)');
        gradient.addColorStop(1, 'rgba(118,203,255,0)');
        context.beginPath();
        context.moveTo(from[0] * width, from[1] * height);
        context.lineTo(to[0] * width, to[1] * height);
        context.strokeStyle = gradient;
        context.lineWidth = weight;
        context.shadowColor = 'rgba(132,214,255,.7)';
        context.shadowBlur = 16;
        context.stroke();
      }
      context.shadowBlur = 0;
      context.globalCompositeOperation = 'source-over';
    };
    const ratio = () => Math.min(devicePixelRatio || 1, 1.5);
    const animated = () => visible && !paused && !reduced.matches && !document.hidden;
    const tick = (time) => {
      frame = requestAnimationFrame(tick);
      if (time - previous < 1000 / FACET_FPS) return;
      previous = time;
      draw(time);
    };
    const restart = () => {
      cancelAnimationFrame(frame);
      previous = -Infinity;
      if (animated()) frame = requestAnimationFrame(tick); else draw(performance.now());
    };
    const resize = () => {
      const rect = host.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      width = rect.width; height = rect.height;
      element.width = Math.round(width * ratio()); element.height = Math.round(height * ratio());
      restart();
    };
    const observer = new IntersectionObserver(([entry]) => { visible = entry.isIntersecting; restart(); });
    const resizeObserver = new ResizeObserver(resize);
    observer.observe(host); resizeObserver.observe(host);
    document.addEventListener('visibilitychange', restart);
    reduced.addEventListener('change', restart);
    resize();
    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect(); resizeObserver.disconnect();
      document.removeEventListener('visibilitychange', restart);
      reduced.removeEventListener('change', restart);
    };
  }, [paused]);
  return <section ref={hero} className="hero" aria-labelledby="hero-title" data-motion-ready="true">
    <div className="facet-fallback" aria-hidden="true" /><canvas ref={canvas} aria-hidden="true" /><div className="hero-vignette" aria-hidden="true" />
    <div className="hero-copy"><div className="eyebrow hero-eyebrow"><span className="tiny-rule" /> A GAME CREATION ENGINE <span className="tiny-rule" /></div><h1 id="hero-title">Creator <span>Engine</span></h1><p className="hero-lead">상상한 세계를, 직접 만드는 도구.</p><p className="hero-description">C++ 런타임과 C# 스크립팅.<br className="mobile-only" /> 에디터에서 플레이어까지 이어지는<br />Windows 게임 제작 환경을 만들고 있습니다.</p><div className="hero-actions"><a className="button primary" href={apiLink('guide/getting-started')}>시작하기 <span>↗</span></a><a className="button secondary" href={apiLink('guide/overview')}>API 살펴보기 <span>→</span></a></div><div className="tech-line"><span>Windows x64</span><b>·</b><span>C++23</span><b>·</b><span>DirectX 12 / Vulkan</span><b>·</b><span>.NET 10</span></div></div>
    <div className="hero-bottom"><a className="scroll-cue" href="#editor">INSIDE THE EDITOR <span>↓</span></a><button className="motion-toggle" aria-pressed={paused} onClick={() => setPaused(!paused)}>{paused ? '조명 애니메이션 재생 ▷' : '조명 애니메이션 일시정지 Ⅱ'}</button></div>
  </section>;
}

function Gallery() {
  const [index, setIndex] = useState(0);
  const dialog = useRef(null);
  const image = asset(captures[index]);
  return <><div className="editor-frame tilted-card"><div className="frame-top"><div className="window-dots"><i /><i /><i /></div><span>Creator Engine <span className="muted">/ Editor</span></span><span className="capture-date">CAPTURED 2026.09.14</span></div><a className="capture-link" href={image} onClick={(event) => { if (!event.metaKey && !event.ctrlKey && dialog.current?.showModal) { event.preventDefault(); dialog.current.showModal(); } }} aria-label={`에디터 스크린샷 ${index + 1} 원본 확대`}><img src={image} alt={`Creator Engine 에디터의 2026년 9월 14일 캡처 ${index + 1}`} loading="lazy" decoding="async" /><span className="expand-label">원본 확대 ↗</span></a><div className="gallery-bar"><div className="gallery-tabs" role="group" aria-label="에디터 스크린샷 선택">{captures.map((_, item) => <button key={item} className={item === index ? 'selected' : ''} aria-pressed={item === index} onClick={() => setIndex(item)}>0{item + 1} <span>에디터 화면</span></button>)}</div><span className="gallery-count">0{index + 1} / 03</span></div></div><dialog ref={dialog} aria-label="에디터 스크린샷 확대"><button className="dialog-close button secondary" onClick={() => dialog.current.close()}>닫기 ×</button><img src={image} alt={`Creator Engine 에디터의 2026년 9월 14일 캡처 ${index + 1}`} /><p>2026.09.14 · Creator Engine Editor</p></dialog></>;
}

const SECTION_SELECTOR = '.brand-page main > section';
const SETTLE_MS = 850;
const EDGE_SLACK = 2;

function useSectionScroll() {
  useEffect(() => {
    const reduced = matchMedia('(prefers-reduced-motion: reduce)');
    /* 절 목록은 마운트 뒤에 읽고, 뷰포트가 바뀌면 다시 읽는다.
       빈 목록을 그대로 쥐고 있으면 기본 스크롤만 막고 아무 데도 가지 못한다. */
    let sections = [];
    let active = 0;
    let transitioning = false;
    let timer = 0;
    /* scrollIntoView는 scroll-padding-top만큼 앞에서 멈춘다. 경계 판정도 같은 값을 써야
       절 하나를 넘는 데 휠이 두 번 들지 않는다. */
    let padding = 0;
    const collect = () => {
      sections = [...document.querySelectorAll(SECTION_SELECTOR)];
      padding = parseFloat(getComputedStyle(document.documentElement).scrollPaddingTop) || 0;
    };
    const nearest = () => {
      if (!sections.length) return 0;
      const center = scrollY + innerHeight / 2;
      const distance = (index) => Math.abs(sections[index].offsetTop + sections[index].offsetHeight / 2 - center);
      return sections.reduce((best, _, index) => distance(index) < distance(best) ? index : best, 0);
    };
    /* 현재 절이 화면보다 길면 그 안쪽은 브라우저에 맡긴다. 마지막 절 아래의 푸터도 같은 이유로 통과시킨다. */
    const scrollableWithin = (direction) => {
      const section = sections[active];
      if (!section) return true;
      return direction > 0
        ? scrollY + innerHeight < section.offsetTop + section.offsetHeight - padding - EDGE_SLACK
        : scrollY > Math.max(0, section.offsetTop - padding) + EDGE_SLACK;
    };
    const targetOf = (direction) => {
      if (transitioning || sections.length < 2 || scrollableWithin(direction)) return -1;
      const next = Math.max(0, Math.min(sections.length - 1, active + direction));
      return next === active ? -1 : next;
    };
    const move = (next) => {
      active = next;
      transitioning = true;
      sections[next].scrollIntoView({ behavior: 'smooth', block: 'start' });
      clearTimeout(timer);
      timer = setTimeout(() => { transitioning = false; active = nearest(); }, SETTLE_MS);
    };
    const wheel = (event) => {
      if (reduced.matches || Math.abs(event.deltaY) < 8) return;
      const next = targetOf(event.deltaY > 0 ? 1 : -1);
      if (next < 0) return;
      event.preventDefault();
      move(next);
    };
    const keys = (event) => {
      if (reduced.matches || event.target.matches('input, textarea, button, a, select, [contenteditable]')) return;
      const direction = ['PageDown', 'ArrowDown', ' '].includes(event.key) ? 1 : ['PageUp', 'ArrowUp'].includes(event.key) ? -1 : 0;
      if (!direction) return;
      const next = targetOf(direction);
      if (next < 0) return;
      event.preventDefault();
      move(next);
    };
    const sync = () => { if (!transitioning) active = nearest(); };
    const remeasure = () => { collect(); sync(); };
    collect();
    sync();
    document.addEventListener('wheel', wheel, { passive: false });
    addEventListener('keydown', keys);
    addEventListener('scroll', sync, { passive: true });
    addEventListener('resize', remeasure);
    return () => {
      clearTimeout(timer);
      document.removeEventListener('wheel', wheel);
      removeEventListener('keydown', keys);
      removeEventListener('scroll', sync);
      removeEventListener('resize', remeasure);
    };
  }, []);
}

function Landing() {
  useSectionScroll();
  return <><a className="skip" href="#main">본문으로 이동</a><Header /><main id="main"><Facets /><section id="editor" className="section showcase"><div className="section-heading"><div><span className="eyebrow">01 / WORKSPACE</span><h2>만드는 순간과<br />플레이하는 순간을 가까이.</h2></div><p>씬을 구성하고, 속성을 조정하고, 실행합니다.<br />하나의 워크스페이스에서 이어지는 작업 흐름.</p></div><Gallery /><p className="caption">실제 에디터 캡처 · 이미지 원본은 저장소의 <code>docs/resource</code>에 보존됩니다.</p></section><section className="section architecture"><div className="section-heading"><div><span className="eyebrow">02 / ENGINE ARCHITECTURE</span><h2>작업은 나누고,<br />실행 기반은 공유합니다.</h2></div><p>화면을 만드는 렌더러, 세계를 갱신하는 런타임,<br />게임을 완성하는 도구의 경계를 분명하게.</p></div><div className="architecture-grid">{[['01','RENDERING','하나의 렌더 경계.','DX12와 Vulkan을 RHI로 분리하고, RenderGraph가 패스 검증·배리어·컬링·자원 수명을 관리합니다.','native/EnhancedRenderGraph'],['02','SCRIPTING','게임 로직은 C#으로.','Component의 생명주기와 물리 전후 틱, SimulationScope를 이용해 게임의 동작을 구성합니다.','script/Component'],['03','BUILD PIPELINE','프로젝트에서 플레이어로.','CreatorBuildTool이 엔진 배포, 스크립트 컴파일, 콘텐츠 cook·PAK와 Player 검증을 연결합니다.','tools/CreatorBuildTool']].map(([number, kind, title, description, href]) => <article className="feature spotlight-card" key={number}><div className="feature-number">{number} <span>{kind}</span></div><h3>{title}</h3><p>{description}</p><a href={apiLink(href)}>{kind === 'SCRIPTING' ? 'Script API' : kind === 'RENDERING' ? 'RenderGraph 구조' : '빌드 도구'} <span>↗</span></a></article>)}</div><div className="architecture-note"><span className="status-dot" /><p><strong>개발 중인 엔진입니다.</strong> 현재 RenderGraph는 선언 순서로 실행합니다. 자동 의존성 정렬과 Launcher 등 후속 계획은 구현된 기능과 구분합니다. 에디터 워크스페이스의 현행 검증 대상은 DX12입니다.</p><a href="https://github.com/29thnight/CreatorEngine/blob/master/README.md">개발 상태 ↗</a></div></section><section className="section scripting"><div className="scripting-copy"><span className="eyebrow">03 / START CREATING</span><h2>작은 동작에서<br />게임이 시작됩니다.</h2><p>엔진이 제공하는 생명주기와 API를 따라 게임의 동작을 구성합니다.<br />물리 스텝 전후처럼 각 단계의 역할에 맞는 지점에서 필요한 로직을 작성할 수 있습니다.</p><a className="text-link" href={apiLink('guide/first-script')}>첫 번째 스크립트 작성하기 <span>→</span></a></div><CodeBlock language="C#" text={'using CreatorEngine;\n\npublic sealed partial class Mover : Component\n{\n    public override void PostPhysics(float tick)\n    {\n        if (!HasTransform) return;\n        if (Input.GetKey(KeyCode.W))\n            Transform.Translate(new Float3(0f, 0f, 2f * tick));\n    }\n}'} /></section><section className="section final-cta"><span className="eyebrow">BUILDING THE NEXT LAYER.</span><h2>엔진 안으로 들어가 보세요.</h2><p>시작 가이드, API의 사용 조건, 소스 코드까지.</p><div className="hero-actions"><a className="button primary" href="api/">문서 · API <span>→</span></a><a className="button secondary" href="https://github.com/29thnight/CreatorEngine">GitHub 저장소 <span>↗</span></a></div></section></main><Footer /></>;
}

function CodeBlock({ text, language = 'C#' }) {
  const [copied, setCopied] = useState(false);
  const code = useRef(null);
  const copy = async () => { try { await navigator.clipboard.writeText(text); setCopied(true); setTimeout(() => setCopied(false), 2000); } catch { const range = document.createRange(); range.selectNodeContents(code.current); const selection = getSelection(); selection.removeAllRanges(); selection.addRange(range); } };
  return <div className="code-panel doc-code"><div className="code-top"><span>{language}</span><button className="copy-button" onClick={copy}>{copied ? '복사됨' : '복사'}</button></div><pre><code ref={code}>{text}</code></pre></div>;
}

function Footer() {
  return <footer className="site-footer"><a className="wordmark" href="./"><img src={asset('site-assets/mark.svg')} width="25" height="25" alt="" /><span>Creator Engine</span></a><p>개인 개발 프로젝트 · Preview</p><a href="https://github.com/29thnight/CreatorEngine/blob/master/README.md#의존성과-라이선스">의존성 · 라이선스 ↗</a></footer>;
}

function Docs() {
  const [data, setData] = useState(null), [error, setError] = useState(false), [hash, setHash] = useState(location.hash), [search, setSearch] = useState(''), [sidebarOpen, setSidebarOpen] = useState(false);
  useEffect(() => { fetch('reference.json').then((response) => response.ok ? response.json() : Promise.reject()).then(setData).catch(() => setError(true)); const listener = () => setHash(location.hash); addEventListener('hashchange', listener); return () => removeEventListener('hashchange', listener); }, []);
  const [id, anchor] = useMemo(() => { try { return (hash.slice(1) || 'guide/overview').split('~'); } catch { return ['invalid']; } }, [hash]);
  const page = data?.pages.find((item) => item.id === id);
  useEffect(() => { if (page) document.title = `${page.title.replace('\n', ' ')} — Creator Engine`; }, [page]);
  const filtered = useMemo(() => !data ? [] : data.pages.filter((item) => search.trim().normalize('NFKC').toLocaleLowerCase().split(/\s+/).filter(Boolean).every((word) => JSON.stringify([item.title, item.summary, item.members || [], item.sections]).normalize('NFKC').toLocaleLowerCase().includes(word))), [data, search]);
  const navigate = (value) => { location.hash = value; setSidebarOpen(false); };
  if (error) return <><Header api /><main className="doc-main"><h1>문서를 불러오지 못했습니다.</h1><p className="doc-loading-error">reference.json을 읽을 수 없습니다. 로컬에서는 HTTP 서버로 실행한 뒤 다시 시도하세요.</p></main></>;
  return <><a className="skip" href="#main">본문으로 이동</a><Header api /><div className="docs-mobile-bar"><button onClick={() => setSidebarOpen(!sidebarOpen)} aria-expanded={sidebarOpen}>☰ 문서 탐색</button><span>CREATOR ENGINE / DOCS</span></div><div className="docs-layout"><aside className={`docs-sidebar${sidebarOpen ? ' open' : ''}`} aria-label="문서 탐색"><div className="sidebar-label">DOCUMENTATION</div><label className="search-box"><span>⌕</span><input type="search" value={search} onChange={(event) => setSearch(event.target.value)} placeholder="API 또는 문서 검색" aria-label="API 또는 문서 검색" autoComplete="off" /><kbd>/</kbd></label>{data && <nav aria-label="문서 목록">{Object.entries(filtered.reduce((groups, item) => ({ ...groups, [item.group]: [...(groups[item.group] || []), item] }), {})).map(([group, pages]) => <section className="nav-group" key={group}><h2>{group}</h2>{pages.map((item) => <a href={`#${item.id}`} aria-current={item.id === id ? 'page' : undefined} key={item.id} onClick={() => setSidebarOpen(false)}><span className="nav-kind">{item.id.startsWith('script/') ? 'C#' : item.id.startsWith('native/') ? 'C+' : item.id.startsWith('tools/') ? '>_' : '↗'}</span>{item.id === 'guide/overview' ? '문서 개요' : item.title.replace('\n', ' ')}</a>)}</section>)}</nav>}<div className="sidebar-footer"><span className="status-dot" /> Preview reference<br /><span>소스 기준 <code>{data?.meta.revision.slice(0, 7)}</code></span></div></aside><main id="main" className="doc-main" tabIndex="-1"><DocContent data={data} page={page} anchor={anchor} navigate={navigate} /></main><aside className="doc-toc"><span className="sidebar-label">ON THIS PAGE</span>{page && <Toc page={page} />}<a className="toc-source" hidden={!page} href={page && sourceUrl(data, page)} target="_blank" rel="noopener noreferrer">소스에서 확인 ↗</a><p className="toc-caption">정의와 설명은 명시된<br />소스 커밋을 기준으로 합니다.</p></aside></div></>;
}

const sourceUrl = (data, page) => `https://github.com/${data.meta.repository}/blob/${data.meta.revision}/${page.source.split('/').map(encodeURIComponent).join('/')}`;
const slug = (value) => value.replace(/[^a-zA-Z0-9_-]+/g, '-').replace(/-$/, '').toLowerCase();
function Toc({ page }) { const sections = [...(page.signature ? [['declaration', '선언']] : []), ...(page.note ? [['contract', '사용 조건']] : []), ...page.sections.map((section) => [section.id, section.title]), ...(page.example ? [['example', '사용 예제']] : []), ...(page.members ? [['members', `주요 멤버 (${page.members.length})`]] : [])]; return <nav>{sections.map(([section, title]) => <a href={`#${page.id}~${section}`} key={section}>{title}</a>)}</nav>; }
function DocContent({ data, page, anchor, navigate }) {
  /* 멤버 링크로 연 첫 화면이 문서 맨 위에 떨어지지 않게 두 가지를 못 박는다.
     effect 시점에는 DOM이 이미 커밋돼 있으므로 requestAnimationFrame을 기다리지 않는다 —
     프레임이 그려지지 않는 창에서는 그 콜백이 오지 않아 이동 자체가 사라진다.
     behavior도 생략하지 않는다. CSS의 scroll-behavior:smooth를 따르면 사용자 조작이 없는
     첫 화면에서 이동이 수행되지 않는다. */
  useEffect(() => {
    if (anchor) document.getElementById(anchor)?.scrollIntoView({ block: 'start', behavior: 'instant' });
    else if (page) scrollTo({ top: 0, behavior: 'instant' });
  }, [page, anchor]);
  if (!data) return <article><div className="eyebrow">CREATOR ENGINE / DOCUMENTATION</div><h1>엔진을 이해하고,<br />첫 동작을 만드세요.</h1><p>문서 목록을 불러오는 중입니다.</p></article>;
  if (!page) return <article><div className="eyebrow">DOCUMENTATION</div><h1>문서를 찾을 수 없습니다.</h1><p>주소에 해당하는 문서가 없습니다. 탐색 메뉴 또는 문서 개요로 이동하세요.</p><button className="button secondary" onClick={() => navigate('guide/overview')}>문서 개요</button></article>;
  const heading = (section, title) => <h2 id={section} key={section}>{title}</h2>;
  const pageIndex = data.pages.indexOf(page);
  return <article><div className="eyebrow">CREATOR ENGINE / {page.group}</div><h1>{page.title.split('\n').map((line, index) => <span key={line}>{index > 0 && <br />}{line}</span>)}</h1><p className="doc-summary">{page.summary}</p><div className="doc-tags"><span>Preview</span><span>{page.id.startsWith('native/') ? 'Engine internals' : page.id.startsWith('script/') ? 'CreatorEngine' : 'Guide'}</span><span>선별 레퍼런스</span></div>{page.signature && <>{heading('declaration', '선언')}<CodeBlock text={page.signature} language={page.id.startsWith('native/') ? 'C++' : 'C#'} /></>}{page.note && <>{heading('contract', '사용 조건')}<aside className="callout"><strong>API 계약</strong><p>{page.note}</p></aside></>}{page.cards && <div className="doc-cards">{page.cards.map(([title, description, target]) => <a className="doc-card" href={`#${target}`} key={target}><span className="arrow">↗</span><span className="eyebrow">EXPLORE</span><h3>{title}</h3><p>{description}</p></a>)}</div>}{page.sections.map((section) => <section key={section.id}>{heading(section.id, section.title)}<p>{section.text}</p>{section.code && <CodeBlock text={section.code} language={section.language === 'cpp' ? 'C++' : 'C#'} />}</section>)}{page.example && <>{heading('example', '사용 예제')}<CodeBlock text={page.example} /><p className="page-index">현재 소스 계약에 맞춘 예제입니다. 네이티브 빌드·실행 검증과 구분합니다.</p></>}{page.members && <>{heading('members', `주요 멤버 (${page.members.length})`)}<p className="page-index">시그니처는 접근 계약을 요약하며 표현식 본문·초깃값·일부 인라인 구현은 생략합니다. 전체 선언은 소스에서 확인하세요.</p>{page.members.map(([name, signature, description, kind]) => <section className="member" id={`member-${slug(name)}`} key={name}><h3>{name}<span className="member-tag">{kind}</span></h3><CodeBlock text={signature} language={page.id.startsWith('native/') ? 'C++' : 'C#'} /><p>{description}</p><a className="member-source" href={`#${page.id}~member-${slug(name)}`}>이 멤버 링크</a></section>)}</>}<div className="source-note">소스 기준 {data.meta.revision.slice(0, 7)} · <a href={sourceUrl(data, page)} target="_blank" rel="noopener noreferrer">{page.source}</a></div><nav className="doc-pagination" aria-label="이전 다음 문서">{data.pages[pageIndex - 1] && <a href={`#${data.pages[pageIndex - 1].id}`}><small>PREVIOUS</small>{data.pages[pageIndex - 1].id === 'guide/overview' ? '문서 개요' : data.pages[pageIndex - 1].title.replace('\n', ' ')}</a>}{data.pages[pageIndex + 1] && <a href={`#${data.pages[pageIndex + 1].id}`}><small>NEXT</small>{data.pages[pageIndex + 1].id === 'guide/overview' ? '문서 개요' : data.pages[pageIndex + 1].title.replace('\n', ' ')}</a>}</nav></article>;
}

/* 본문 클래스는 렌더 전에 세운다. effect 로 미루면 자식 effect 가 먼저 돌아
   `.brand-page main > section` 같은 선택자가 빈 결과를 잡는다. */
export const applyPageClass = () => { document.body.className = isApiPage() ? 'docs-page' : 'brand-page'; };

export default function App() {
  return isApiPage() ? <Docs /> : <Landing />;
}
