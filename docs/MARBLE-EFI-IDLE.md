# Marble EFI little-core waiting experiment

Status on 2026-10-26: v3 passed the pinned 18-second, four-core black
sleeping-init test. GPU was disabled. Both event-source and CPU-request leases
restored, then Android screen/actual OFF was confirmed. Battery readings were
31.8 C before boot and 32.6 C after return. These are two Android observations,
not continuous in-iOS battery telemetry. Keep all earlier evidence and manifests.

The previous CPU-cap experiment reached its conservative 45 C die guard
during GPU-disabled iOS startup. Its hottest reported channel was in the
little-core region. This suggests an isolation experiment; it does not prove
that the resident EFI core is the sole heat source. That core still executes
a 20,000-iteration busy pause between USB/touch/thermal polls, even when XNU's
four big cores are idle. The new path replaces that pause with a local event
wait only when explicitly requested and after loader-only qualification.

## Mechanism and references

Use CNTKCTL_EL1's virtual counter event stream and WFE, following the ARM
register description and the existing Linux arm_arch_timer event-stream
mechanism. Archive URLs and SHA256 values accompany the candidate. This
register is already saved and restored by the working loader return stub.
The earlier CNTP register trap is not a reason to touch physical timers again.

Only EVNTEN, EVNTDIR, EVNTI and EVNTIS change. Clear EVNTIS to use CNTVCT
bits 0..15, select rising transitions, and choose the longest period no more
than 500 us. At 19.2 MHz this is bit 12, 8192 ticks, about 426.67 us. The
event source is local to this PE and does not require an unmasked GIC IRQ.
All other CNTKCTL fields are preserved. No CNTP/CNTV timer, GIC, little-core
EPSS, voltage, PLL, PMIC or charging writes are added. The big/prime CPU cap
from the previous experiment remains mandatory.

The 500 us bound describes the architected event period, not a guaranteed
USB/touch service latency or a software escape from a broken hardware event
source. WFE may return spuriously, be trapped/emulated, or be delayed by the
hypervisor. This PE cannot execute a software timeout while asleep if its
event source does not work. Marble has no enabled watchdog. Therefore the
first hardware step is a short, loader-only probe; it has not run yet.

## Ownership, diagnostics and failure behavior

`oem idle-probe` requires an idle, clean loader. At EL1 it saves CNTKCTL,
enables the event stream, reads the entire register back, clears one stale
local event, and measures 64 WFE waits. Each measured wait masks DAIF only
around source readback and WFE, then restores the previous DAIF. The probe
reproduces the later interrupt-masked watching state without starting XNU,
userspace or GPU. It restores CNTKCTL and requires exact readback before
qualification. A probe with all immediate returns cannot pass. A failed probe
or runtime fault is latched rather than automatically retried.

`--efi-idle` adds `marble_efi_idle=1`; both host and EFI require the successful
probe, the CPU-cap profile, an off-core run and a budget of 1..90 seconds.
The run owns a separate event-stream lease, acquired before CPU_ON and restored
on every return from the other-core wait, including a failed CPU_ON. Normal
XNU big-core request restoration still requires all participating cores OFF.
If control readback drifts or a completed WFE lasts over four periods, further
WFE instructions are disabled, stop is requested, and the attempt is latched.
A missing event cannot be detected until WFE returns; the probe qualification
does not make that hardware limitation disappear.

v1 changed the former non-block busy pause and NoEvent USB waits during an
opted-in XNU run. v2/v3 also use short-lived event-stream leases for small
command-receive and cable-state waits, after the loader-only probe qualifies
that boot. Each lease is restored before command dispatch, so no lease leaks
into payload setup or reboot. Bulk upload waits outside XNU keep the old
behavior. `oem idle-stats` reports completed standby leases and restore failures.
USB responses still sample touch. Diagnostics record wait count,
time across WFE instructions, immediate/late returns, maximum wait and maximum
observed poll gap. These measure elapsed wait time, not hardware C-state
residency, watts, battery temperature, or a guaranteed touch latency.

## Original offline artifacts

- `out/loader-marble-efi-idle-v1.efi`
- `out/marble-efi-idle-v1-loader-boot.img` and matching hash manifest
- `out/efi-idle-v1-reference.tar.gz`
- `out/efi-idle-v1-plan/validation.json`

The kernel is the unchanged `out/linux-build/mach-marble-cpucap-v3`, SHA256
`de4f2fe9e573522fa1580d95e7069567ccc6599e4766f3cf5c3655e8d80aa6d4`.
Its failed iOS/GPU prerequisite and small-black-idle restriction remain in
force. A loader change is not a reason to erase those results or silently
relabel the kernel as a new accepted build.

When Android ADB is authorized, record battery temperature and screen state,
then temporarily boot the new image with `tools/marble-run.py boot --image
out/marble-efi-idle-v1-loader-boot.img`. Do not flash partitions. The first
test uses only a 4 MB RAM image, black pixels, GPU off, four XNU cores and an
18-second budget:

```
python tools/marble-thermal-idle-test.py --out out/NEW-EVIDENCE-DIRECTORY --cpus 4 --cpu-cap --efi-idle --kernel out/linux-build/mach-marble-cpucap-v3
```

The runner saves the loader-only probe separately and does not start XNU if
the probe fails. Audit normal return, CNTKCTL restoration, big/prime request
restoration, temperature samples and wait statistics, then return to Android
and confirm screen OFF. Black pixels alone do not mean panel power-off.
Do not run the old stopped iOS profile, GPU or unbounded UI stress to qualify
this change. Full userspace needs a later explicit experiment built from the
measured idle result. GPU correctness, true hardware VSync and daily hardware
support remain unfinished, in that order.

Battery below 40 C remains the user's target. There is still no continuous
in-iOS battery-temperature measurement, so pre/post Android readings cannot
establish its peak. The existing 45 C die stop is retained as an independent
conservative experiment guard; short die excursions are not battery readings.

## Hardware evidence and revised short-test policy

The user clarified that transient 40..50 C chip readings are normal for their
phone; battery temperature and the duration of heating are the priorities.
The earlier strict die thresholds were experiment isolation criteria, not
battery limits or a diagnosis of overheating. v1/v2 attempts hit 40.8, 40.4,
and exactly 40.0 C cold die refusals. All three returned to Android screen OFF
without starting XNU/GPU. The original records are retained in
`out/efi-idle-v1-idle-smp4`, `out/efi-idle-v1-idle-smp4-cooled`, and
`out/efi-idle-v2-idle-smp4`. No frequency or event leases remained active.

v2's 30-second qualified standby observation is in `out/efi-idle-v2-standby`:
29.824 seconds across WFE instructions, maximum observed poll gap 559 us,
no late return or restoration failure; die samples 38.9, 38.5 and 38.9 C.
This demonstrates the wait path, not measured power or an Android comparison.

v3 adds an explicit `--short-thermal` profile, restricted by host and EFI to
the exact existing 4 MB sleeping-init image, black pixels, no GPU/probes,
no resident system disk, both CPU/event leases, and an 18-second budget.
It permits a starting die reading below 50 C. A reading >=55 C, invalid/stale
telemetry, or a span of 10 seconds of consecutive >=50 C observations requests
stop. Cooling observations below 50 C reset that consecutive span. The logs
record current and maximum observed hot spans. This span does not establish
temperature between samples, and the 55 C response is reactive, not a
guaranteed peak. The old default 40/45 C profile stays separate; stopped
iOS/GPU manifests are not deleted or loosened.

The short profile also requires an Android battery record for serial YOUR_ANDROID_SERIAL,
no older than 180 seconds, below 38 C to leave headroom to the user's 40 C
target. After a normal return the runner confirms Android screen OFF and
records battery again; a post-return reading >=40 C fails the report. This
cannot enforce or certify the unobserved in-iOS battery peak. The existing
redwood charger protocol layout is not verified for marble and is not enabled
just to obtain a temperature number; continuous battery monitoring remains
future work.

`out/efi-idle-v3-idle-smp4` passed: actual CPU cap acquisition/readback on four
cores; 18 running thermal samples, highest 39.6 C, maximum sample gap
1,000,109 us. No >=50 C sample. 87,896 WFE instructions covered 17.631 seconds,
maximum completed wait 423 us, maximum observed poll gap 465 us, late=0.
All three XNU secondary cores reported OFF; event control restored from c4 to
the saved 0, and all big/prime request/mirror snapshots restored and read back.
Android battery before boot 31.8 C, after return 32.6 C, screen/actual OFF.
There was one actual XNU run and zero GPU runs in this follow-up.

This is only short sleeping-init acceptance. It does not measure a thermal
improvement against the old 39.2 C idle run, qualify touch latency, exercise
the >24-second interrupt-masked watching phase with XNU, or accept iOS/GPU
startup and sustained rendering. The current kernel and its old stopped
iOS/GPU manifest are unchanged. Latest loader:
`out/marble-efi-idle-v3-loader-boot.img`; source `out/loader-marble-efi-idle-v3.efi`.
Build and validation records are in `out/efi-idle-v3-plan`.

For a later qualified repetition, record a fresh Android battery snapshot with
`marble_android.battery_snapshot` before reboot, temporary boot v3, then run:

```
python tools/marble-thermal-idle-test.py --out out/NEW-EVIDENCE-DIRECTORY --cpus 4 --cpu-cap --efi-idle --short-thermal --battery-preflight out/FRESH-ANDROID-BATTERY.json --kernel out/linux-build/mach-marble-cpucap-v3
```

Further useful work is reliable in-session battery telemetry and bounded
load/startup validation before resuming GPU correctness and real hardware
VSync. Do not infer acceptance of those stages from the passing idle result.
