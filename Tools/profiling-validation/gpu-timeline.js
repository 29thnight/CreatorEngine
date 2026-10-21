// Integrated into the existing CEPROF CPU/ETW viewer, which owns D and A/B.
let gpuHits = [];

async function attachGpuData(dataset) {
    const blob = document.getElementById('gpu-blob-' + dataset.id);
    if (!blob) {
        throw new Error('GPU dataset is missing: ' + dataset.id);
    }
    const raw = atob(blob.textContent.trim());
    const bytes = Uint8Array.from(raw, character => character.charCodeAt(0));
    dataset.gpu = JSON.parse(await new Response(
        new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text());
    if (dataset.gpu.summary.sourceSha256 !== dataset.nativeSha256) {
        throw new Error('CPU/GPU source capture mismatch');
    }
}

function gpuUnion(intervals) {
    const merged = [];
    for (const interval of intervals.sort((a, b) => a[0] - b[0])) {
        const last = merged.at(-1);
        if (last && interval[0] <= last[1]) {
            last[1] = Math.max(last[1], interval[1]);
        } else {
            merged.push([...interval]);
        }
    }
    return merged.reduce((sum, interval) => sum + interval[1] - interval[0], 0);
}

function gpuPassName(marker) {
    return D.markers[marker].replace(/ \[gpu scope \d+\]$/, '');
}

function drawGpuTimeline() {
    if (!D.gpu) {
        return;
    }
    const view = Number($('view').value);
    const query = $('gpuSearch').value.toLowerCase();
    const records = ranged(D.gpu.events, A, B, D.gpu.maxDuration).filter(event =>
        (!view || event[4] === view) && (!query || gpuPassName(event[2]).toLowerCase().includes(query)));
    const groups = new Map();
    for (const event of records) {
        const key = event[3] + ':' + event[4];
        if (!groups.has(key)) {
            groups.set(key, []);
        }
        groups.get(key).push(event);
    }
    const layouts = [];
    let height = 28;
    for (const [key, events] of groups) {
        const ends = [];
        const lanes = events.map(event => {
            let lane = ends.findIndex(end => end <= event[0]);
            if (lane === -1) {
                lane = ends.length;
            }
            ends[lane] = event[0] + event[1];
            return lane;
        });
        const rowHeight = Math.max(36, ends.length * 16 + 20);
        layouts.push({key, events, lanes, y: height, height: rowHeight});
        height += rowHeight;
    }
    const {ctx, w} = canvas('gpuPasses', Math.max(70, height));
    ctx.clearRect(0, 0, w, Math.max(70, height));
    grid(ctx, w, Math.max(70, height));
    const coordinate = time => 170 + (time - A) / (B - A) * (w - 170);
    gpuHits = [];
    const totals = new Map();
    const queueIntervals = new Map();
    for (const row of layouts) {
        const [queue, viewId] = row.key.split(':');
        ctx.fillStyle = ink();
        ctx.fillText('GPU Q' + queue + ' · V' + viewId, 5, row.y + 12);
        ctx.fillText((D.gpu.queues[queue] || '').slice(0, 22), 5, row.y + 27);
        row.events.forEach((event, index) => {
            const name = gpuPassName(event[2]);
            const left = coordinate(Math.max(A, event[0]));
            const end = Math.min(B, event[0] + event[1]);
            const width = Math.max(1, coordinate(end) - left);
            const top = row.y + row.lanes[index] * 16;
            ctx.fillStyle = event[4] === 2 ? '#177487' : '#966000';
            ctx.fillRect(left, top, width, 12);
            if (width > 100) {
                ctx.save();
                ctx.beginPath();
                ctx.rect(left + 2, top, width - 4, 13);
                ctx.clip();
                ctx.fillStyle = '#ffffff';
                ctx.fillText(name + ' ' + fmt(event[1]) + 'ms', left + 3, top + 10);
                ctx.restore();
            }
            const clipped = Math.max(0, end - Math.max(A, event[0]));
            gpuHits.push({x: left, y: top, w: width, h: 13, text:
                `${name}\nGPU queue ${event[3]} · view ${event[4]} · submission ${event[5]}` +
                `\nsource engineFrame ${event[6]} · realFrame ${event[7]}` +
                `\nframeKind ${event[8]} · generatedOrdinal ${event[9]}` +
                `\n${fmt(event[0])} → ${fmt(event[0] + event[1])}ms` +
                `\n원본 ${fmt(event[1])}ms · 현재 범위 ${fmt(clipped)}ms` +
                `\nQPC ticks ${event[10]} → ${event[11]}\nCPU/GPU 공통 QPC · 수집 시점 아님`});
            const key = event[4] + ' / Q' + event[3] + ' / ' + name;
            const total = totals.get(key) || {sum: 0, calls: 0, max: 0};
            total.sum += clipped;
            total.calls++;
            total.max = Math.max(total.max, event[1]);
            totals.set(key, total);
            if (!queueIntervals.has(event[3])) {
                queueIntervals.set(event[3], []);
            }
            queueIntervals.get(event[3]).push([Math.max(A, event[0]), end]);
        });
    }
    if (!records.length) {
        ctx.fillStyle = ink();
        ctx.fillText('현재 범위에 대응 GPU 패스 없음 · CPU/GPU 확대 버튼으로 확인', 175, 47);
    }
    $('gpuStats').innerHTML = [...totals].sort((a, b) => b[1].sum - a[1].sum).map(([name, value]) =>
        `<tr><td>${escapeHtml(name)}</td><td>${fmt(value.sum)}</td><td>${value.calls}</td><td>${fmt(value.max)}</td></tr>`).join('') ||
        '<tr><td colspan="4">현재 시간 범위·뷰·검색 조건에 GPU 패스 없음</td></tr>';
    const queueText = [...queueIntervals].map(([queue, intervals]) => {
        const first = intervals.reduce((value, interval) => Math.min(value, interval[0]), Infinity);
        const last = intervals.reduce((value, interval) => Math.max(value, interval[1]), -Infinity);
        return `Q${queue}: 계측 합집합 ${fmt(gpuUnion(intervals))}ms / 첫~마지막 폭 ${fmt(last-first)}ms`;
    }).join(' · ');
    $('gpuStatus').textContent = `${D.gpu.events.length.toLocaleString()} 원본 GPU spans · 범위 내 ${records.length}개 · ` +
        `${D.gpu.summary.exactProvenanceMatches.toLocaleString()} exact 귀속 · ` + queueText;
    window.CETimelineGpu = {dataset: D.id, total: D.gpu.events.length, visible: records.length,
        queues: [...queueIntervals.keys()], exactMatches: D.gpu.summary.exactProvenanceMatches};
}

function fitCpuAndGpu() {
    if (!D?.gpu) {
        return;
    }
    const view = Number($('view').value) || (Number($('unit').value) || 2);
    const middle = (A + B) / 2;
    const distance = interval => Math.max(interval[0] - middle, middle - interval[0] - interval[1], 0);
    const candidates = D.measurements.filter(measurement => measurement[3] === view);
    candidates.sort((a, b) => distance(a) - distance(b));
    const cpu = candidates[0];
    if (!cpu) {
        return;
    }
    const spans = D.gpu.events.filter(event => event[4] === view && event[7] === String(cpu[2]));
    if (!spans.length) {
        $('gpuStatus').textContent = `realFrame ${cpu[2]} / view ${view}: 원본 GPU 귀속 없음`;
        return;
    }
    const begin = Math.min(cpu[0], ...spans.map(event => event[0]));
    const end = Math.max(cpu[0] + cpu[1], ...spans.map(event => event[0] + event[1]));
    setRange(begin - .25, end + .25);
}

$('gpuFit').onclick = fitCpuAndGpu;
$('gpuSearch').oninput = drawGpuTimeline;
for (const type of ['mousemove', 'click']) {
    $('gpuPasses').addEventListener(type, event => {
        const bounds = $('gpuPasses').getBoundingClientRect();
        const x = event.clientX - bounds.left - $('gpuPasses').clientLeft;
        const y = event.clientY - bounds.top - $('gpuPasses').clientTop;
        const selected = [...gpuHits].reverse().find(hit => x >= hit.x && x <= hit.x + hit.w && y >= hit.y && y <= hit.y + hit.h);
        if (type === 'click') {
            if (selected) {
                $('inspect').textContent = selected.text;
            }
            return;
        }
        $('tip').style.display = selected ? 'block' : 'none';
        if (selected) {
            $('tip').textContent = selected.text;
            $('tip').style.left = Math.max(4, Math.min(innerWidth - 340, event.clientX + 12)) + 'px';
            $('tip').style.top = Math.max(4, Math.min(innerHeight - 200, event.clientY + 12)) + 'px';
        }
    });
}
$('gpuPasses').addEventListener('mouseleave', () => { $('tip').style.display = 'none'; });
