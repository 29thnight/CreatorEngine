# CreatorEngine local hardening

Base: Microsoft/DxTimingCaptureLibrary `05bf5ff1d9e1b1d63c7420a166e61ded2ca7c072`.
The original 48-file hashes are retained in SOURCE_MANIFEST.json. The MIT license and copyright
notices remain unchanged. Four files differ from that upstream source baseline:

- `include/DxTimingCaptureLibrary/EventData.h`: bounded null-terminated string scans, remaining-length
  pointer checks before advancing, and scalar `memcpy` reads instead of potentially unaligned typed reads
  - Git blob: `a1dfdb73517dd58e15646d899f03113be4e4e2bf`
- `lib/PerProcessData.h`: require complete runtime marker records, valid thread ordinals and bundle
  payloads; reject empty/self-referencing bundle lookup; bound markers per list; guard empty execution
  starts, history extents and 1-based runtime marker indices; validate timestamp precision and full pairs
  - Git blob: `32990ed8b9559d632efd1b5449dae8e8c1b25a82`
- `lib/AdapterTracker.h`: validate the raw engine enum, cap node ordinals to 1,024, widen before `+1`
  - Git blob: `14ff0a4a3476feb56c12d19dea686bcc8a4bde86`
- `lib/MemoryCounterWriter.h`: validate raw priority class and counter enum before array indexing
  - Git blob: `93f2b74d843b53720e4491a9be1cece5cac84dea`

Malformed input throws through the existing ETW exception path and ends the helper's capture with
an incomplete/error status. This does not establish that every decoder path is safe. The findings
are static code observations; there was no malicious-event execution or exploit reproduction.
No Windows build, tests, capture, SDK installation or permission change was performed.

The integration rejects elevated Editor/helper tokens and contains no UAC/runas retry. A complete
decoder audit and authorized Windows malformed-event tests are prerequisites to reconsidering
privileged decoding. Administrator launch is not a workaround.
