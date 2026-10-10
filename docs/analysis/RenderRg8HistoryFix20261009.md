# RG8 normal timing history: SSGI ping-pong identity fix

Base: `d5a6a26a`, with preserved local changes described in
[the initial measurement](RenderRg8Pr168Measurement20261009.md). VS 2026 Release,
DX12, RTX 4070 Ti, TestShadow 1496 x 692. No RG8 completion or adoption claimed.

## Proven cause and correction

Temporary evidence-only diagnostics showed that the immediately preceding
submission was stored and passed `FindFresh`, but its structural signature did
not match the next graph. The first differing byte was consistently offset
11558. Decoding both signatures identified `SSGI.History0`'s version count,
followed by the history/depth indices used by `SSGI.Resolve` and
`SSGI.StoreHistory`.

SSGI imported both textures in physical slot 0/1 order while alternating which
slot it reads/writes. Thus equivalent consecutive frames had different declared
resource/version topology. The history cache correctly rejected the mismatch;
its latest-sample replacement meant the usable parity was usually overwritten.

`EnhancedSSGIPass::Declare` now imports by logical role relative to the current
write slot. Role0 is the current destination and Role1 the previous history.
The physical handles, states, writeback pointers, read/write array indexing and
retirement rules are unchanged. Structural matching and freshness thresholds are
unchanged. Temporary signature dumps were removed before the final build.

## Remeasurement

The same-process forward 0/1/2 and reverse 2/1/0 runs each collected 64 ordinary
candidates after 16 warmup frames. Every segment accepted its first 32 eligible
samples with **zero rejected candidates**, totaling 192 accepted frames.
Actual schedule and calibrated intervals were independently verified using the
existing audit functions. This resolves the earlier history-input failure.

| Order / mode | GPU median / p95 ms | Full-view CPU median / p95 ms | Record-submit CPU median ms |
|---|---:|---:|---:|
| Forward 0 | 2.2031 / 4.5978 | 10.9135 / 14.7043 | 1.3226 |
| Forward 1 | 1.6640 / 2.8959 | 7.2981 / 12.8352 | 1.5368 |
| Forward 2 | 3.0300 / 4.9868 | 13.4318 / 15.5975 | 7.2631 |
| Reverse 2 | 2.2129 / 4.3346 | 14.2176 / 18.2816 | 6.6644 |
| Reverse 1 | 2.2267 / 2.7658 | 9.0580 / 11.3100 | 1.8869 |
| Reverse 0 | 2.7561 / 3.4017 | 7.1646 / 11.7650 | 0.9039 |

Mode2 submitted compute in 55/64 accepted frames; the remaining nine had complete
cost history and selected `insufficient-gain`. All 64 measured overlaps were zero.
Mode2 scheduling CPU alone was median **4.2045 ms forward / 3.8823 ms reverse**,
with recording at 1.5986 / 1.5967 ms and submission at 0.5987 / 0.6248 ms.
Therefore fixing the history exposes a material scheduling cost; it does not
establish async gain. Clocks were not locked, evidence serialization remains
enabled, and forward/reverse GPU rankings are inconsistent.

Examples of executed compute work include Geometry.Visibility reset/cull,
Geometry.Occlusion mip construction, and SSAO. Compute submissions must not be
equated with concurrent GPU work. The absence of measured overlap is confirmed;
its specific remaining cause is not established by this change.

## Validation and remaining work

Release build passed (existing PDB LNK4020 warnings retained). The final binary
passed native execution 428 checks, queue planning 111 checks, full RenderGraph
regression and the native GPU-validation gate. Product capture comparisons are
collected separately from timing: 384 logical attachment/stage comparisons across
pre-fix versus fixed mode0 and fixed mode0 versus modes1/2 were exactly equal,
with matched controlled inputs, no nonfinite values and maximum error zero.
Shared stage readbacks are counted by logical stage name, not as independent GPU
work. Both timing processes and all six capture processes exited normally.
Product GPU validation, Debug/Vulkan execution,
and broader workload acceptance are not claimed here.

Keep default OFF. Next investigate the per-frame candidate search cost and the
actual waits/batches/pass intervals that prevent selected queues from overlapping.
Do not relax measurement completeness or structural equality to force compute.

Evidence: `Build/Verification/Phase43/RG8History20261009/` contains `probe`,
`signature-probe`, decoded `signature-difference.txt`, `fixed-normal`,
`fixed-native`, `fixed-captures`, binary identities, `summarize-fixed.py`, and
`fixed-performance-summary.json`. Build log: `Build/rg8-history-fixed.log`.
