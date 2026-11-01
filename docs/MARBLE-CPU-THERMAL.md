# Marble CPU request cap — 2026-10-26

This is a tested, reversible CPU request limiter, not a completed GPU thermal
fix or battery-temperature controller. No Android /data or partitions are used.

## What changed

Qualcomm firmware changes EPSS requests during CPU_ON. A cap applied before
CPU_ON was ineffective: the later XNU check saw the inherited high requests
and panicked before userspace/GPU. Acquisition now happens through a RAM
mailbox after all four XNU cores start and before redwood_adreno_init/userspace.

The loader validates both complete normal OPP sequences against Android's
read-only policy4/policy7 tables and marble DT, including the repeated terminal
row and selected core count 4. It then lowers only requests above the caps:

| Domain | Observed inherited request | Selected cap |
| --- | ---: | ---: |
| big (CPU4/5/6) | row15, 2342400 kHz | row5, 1209600 kHz |
| prime (CPU7) | row17, 2822400 kHz | row3, 1171200 kHz |

These are requested OPPs, not effective-clock measurements. A lower inherited
request is kept lower. Only four exact MMIO addresses may be written:
0x17d92320, 0x17d93320 (PERF_STATE), 0x17d09808, 0x17d0980c
(the DT perf-lock mirrors, in the order used by MiCode target_index).
No domain0/little, voltage, PLL, LUT, PMIC or charging registers are written.
DT skip-enable-check is honored; no speculative enable-register access is added.

Each write is read back. Failed acquisition rolls back. Normal return restores
the post-CPU_ON snapshots only after all CPU4..7 report PSCI OFF, verifies every
register including unchanged domains, and latches dirty on incomplete restore.
A hung kernel keeps the lower requests and blocks reuse. XNU verifies the
mailbox ABI, supported caps, actual rows and bounds before GPU/userspace.
The mailbox uses an isolated 8KB allocation in the EFI image, uncached XNU
mapping and explicit loader cache maintenance for its single request/reply.

## Exact artifacts

- Loader: out/marble-cpucap-v2-loader-boot.img, manifest of the same name.
- Kernel: out/linux-build/mach-marble-cpucap-v3 and matching JSON.
- ABI: MCPU-MARBLE-CAP-CHECK-v2 and getvar marble-cpucap =
  big-prime-restore-1p2ghz-v2. Both must match; --cpu-cap is mandatory.
- Source: out/cpucap-v3-source.tar.gz plus host sources in the recovery point.
- References: out/cpucap-reference.tar.gz. MiCode commit
  48952ed36228217531482b39d5bef13e7fd808ec, Linux v6.12, and extracted marble DT.
  Original URLs and SHA256 digests are inside the archive.

Cold preflight remains strictly below 40 C die. This cap experiment uses a
conservative 45 C die stop, sampled each second, while other profiles retain
55 C. This is a reactive die-temperature guard, not a guarantee about peak
temperature and not the user's battery 40 C target. Live battery sensing in
iOS is still missing. A brief die reading above 45 C alone is not proof of
danger or battery overheating. Do not confuse a guard stop with that conclusion.

## Hardware evidence

1. out/thermal-cpucap-idle-smp4: first loader refused a NUL ABI-marker mismatch;
   XNU never started. The affected kernel is marked FAILED.
2. out/thermal-cpucap-v2-idle-smp4: pre-CPU_ON acquisition was overwritten by
   firmware, and XNU's check panicked before GPU/userspace. The old loader
   incorrectly labeled a no-write restore as PASS despite drift. This is
   explicitly NOT valid restore evidence. The corrected restore audits every
   register; the old kernel is marked FAILED.
3. out/thermal-cpucap-v3-idle-smp4: four cores, black pixels, GPU off, sleeping
   init, 18-second budget. Actual four-register lowering and restoration passed.
   18 valid running samples, peak 39.2 C. All secondaries OFF, normal return,
   Android screen/actual OFF. Black framebuffer is not panel power-off.
4. out/thermal-cpucap-v3-ios-cpu: 2.6GB upload followed by a 40.4 C cold
   preflight refusal; no XNU/GPU ran. Returned to Android and confirmed OFF.
5. out/thermal-cpucap-v3-ios-cpu-cooled: after a separate Android cooling
   interval, cold preflight 39.6 C. Full iOS, GPU disabled. Both caps verified;
   SpringBoard/launchd startup logged. Thirty runtime samples, final peak
   46.3 C (controller1/sensor4, little-core region) at roughly 30 seconds.
   The conservative 45 C guard requested stop. Kernel returned normally,
   all CPU request/mirror snapshots restored and checked; Android screen OFF.
   This is a thermal-guard stop, not 60-second or sustained-power acceptance.
   Battery was 29.7 C before the attempt and 30.9 C after return/readback;
   neither is an in-iOS continuous measurement or a guarantee about its peak.

The last live host snapshot saw only 44.7 C. Final stitched logs contain the
46.3 C guard stop, so final-log evidence takes precedence over the host snapshot.
No new GPU runtime was attempted. Do not compare the GPU-disabled 46.3 C
directly with the earlier GPU startup's 53.8 C as a controlled cooling delta.

## Verification and next work

Native fixtures cover complete LUT checks, bad sources/rows/core counts,
read failures before writes, stale ownership, every failed write with rollback,
readback failures, lower-than-cap inherited values, restore failure latch and
firmware drift on a no-write domain. Policy tests cover bounded/single cap args.
22 Python tests pass, including rejecting unknown/loosened log thresholds and
blocking stopped startup profiles while retaining the small black idle profile.
Kernel compilation/linking succeeded; four pre-existing export-list checks
still fail and are retained in kernel-cpucap-v3.log; no new compiler/link error.

Keep the GPU-failed idle-fix kernel and both failed CPU-cap kernels blocked.
The current cap kernel's iOS startup profile is recorded as a conservative
thermal-stop result, with further iOS/GPU use blocked pending a corrected
candidate. The passed 18-second idle evidence remains valid independently.

Next isolate the resident EFI little-core polling work and its inherited
request; then obtain reliable battery-temperature telemetry. Do not disable
temperature guards or repeatedly run this stopped profile. GPU correctness,
real hardware VSync and daily hardware support still need their own acceptance.

The event-stream wait follow-up has now passed a limited hardware test; see
[MARBLE-EFI-IDLE.md](MARBLE-EFI-IDLE.md). v2 also replaces qualified loader
command standby polling with short WFE leases, restored before dispatch.
The v3 test ran four cores for 18 seconds with the exact 4 MB sleeping-init
disk, black pixels and GPU disabled. Runtime die peak was 39.6 C across 18
samples; maximum sampling gap was 1,000,109 us. WFE elapsed time was
17,630,528 us; maximum observed polling gap 465 us, no late waits. CPU
request/mirror and event-register restoration passed; all secondaries OFF.
Battery was 31.8 C before boot and 32.6 C after returning to Android, where
screen/actual OFF was confirmed. This follow-up added one XNU run and no GPU
runs. Evidence: out/efi-idle-v3-idle-smp4 and out/efi-idle-v3-plan.

Following the user's clarification, an explicit --short-thermal profile treats
short 40..50 C die readings separately from battery overheating: start below
50 C; stop on >=55 C or an observed consecutive >=50 C span of 10 seconds,
or invalid/stale readings. It only admits the fixed 18-second black idle
image with CPU/event leases and a fresh Android battery reading below 38 C.
Post-return battery >=40 C fails the report. The older thresholds remain
historical experimental guards, not established device temperature limits.
Neither the sample spans nor pre/post battery readings establish the in-iOS
battery peak. No continuous battery monitoring, touch latency, >24-second XNU
watcher phase, full iOS startup or sustained GPU qualification is claimed.
Old stopped profiles remain recorded; further work needs a distinct candidate
and reliable battery telemetry before bounded load/startup and GPU validation.
