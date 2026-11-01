# Upstream and third-party notices

This is an experimental marble port of HackiOS, not an official upstream release.

- Kernel/loader baseline: OOSEX GANG's `hackios-xnu-kernel`, commit
  `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`. The repository is now named
  <https://github.com/oosexgang/hakios-xnu-kernel>. The original root `LICENSE`
  and file notices are retained. The root license is APSL 2.0.
- Integrated userland source: <https://github.com/oosexgang/hackios-xnu-userland>,
  commit `906425b75e02b4f99eb5d7bb1ef7c9583472d772`. Its MIT license is retained
  at `userland/LICENSE`. Local marble changes include the merged `build.sh`,
  EAGL/injection/touch sources and `mkjobs.py` behavior.
- Apple XNU is obtained separately at the pinned revision documented in
  `docs/MARBLE-BUILD.md`; retain its own license and notices when obtaining it.
- Mesa patches, Mu/Qualcomm interface references, Adreno/SMMU source adaptations,
  LZ4, LZMA and SHA256 code retain their per-file notices and adjacent attribution
  manifests. See `xnu/files/bsd/dev/marble_gpu/upstream/` and `xnuloader/vendor/`.

The root license does not replace distinct licenses on incorporated components.
This publication adds no Apple IPSW, iOS filesystem, SDK, vendor firmware, ROM,
private key, virtual machine or phone userdata. Upstream's existing prebuilt files
remain historical redwood/QEMU inputs; they are not marble releases.
