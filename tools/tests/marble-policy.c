#include "../../xnuloader/marble_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=24 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_audio_stream=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=24 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=24 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_audio_stream=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_audio_refill=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_refill=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_audio_refill=1 marble_audio_refill=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_board_gate=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=1 marble_audio_board_gate=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_pcm=1 marble_audio_board_gate=2 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner_periods=8 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_owner=1 marble_audio_owner_periods=8 marble_audio_owner_periods=8 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_transport=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_transport=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 marble_audio_transport=1 marble_audio_transport=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=1 marble_audio_queue=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_audio_queue=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=19"));
    assert(!marble_arguments_ok("bootcpu=7 marble_audio_queue=1 marble_audio_queue=1 marble_cpu_cap=1 marble_efi_idle=1 marble_thermal_short=1 fb_blank=1 loader_return_after=18"));

    const char *manual="bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=600 marble_gpu_manual=1 fbscale=2";
    assert(marble_arguments_ok(manual));
    const char *manual_bad[]={
        "bootcpu=7 marble_gpu_manual=1 loader_return_after=600",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=600",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=601 marble_gpu_manual=1",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=90 marble_gpu_manual=1",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=600 marble_gpu_manual=1",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 loader_return_after=600 marble_gpu_manual=1",
        "bootcpu=7 cpus=1 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=600 marble_gpu_manual=1",
        "bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=600 marble_gpu_manual=1 marble_gpu_manual=1"
    };
    for(unsigned i=0;i<sizeof(manual_bad)/sizeof(manual_bad[0]);i++)assert(!marble_arguments_ok(manual_bad[i]));
    const char *scaled="bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=90 fbscale=2";
    assert(marble_arguments_ok(scaled));
    assert(!marble_arguments_ok("bootcpu=7 fbscale=2"));
    assert(!marble_arguments_ok("bootcpu=7 fbscale=3"));
    assert(!marble_arguments_ok("bootcpu=7 fbscale=0"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=90 fbscale=2 fbscale=2"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=4 gl=1 marble_gpu_probe=17 marble_gpu_runtime=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_preview=1 fb_hw_vsync=1 loader_return_after=91 fbscale=2"));
    assert(marble_arguments_ok("bootcpu=7 epoch=1577836800"));
    assert(marble_arguments_ok("bootcpu=7 epoch=4102444799"));
    assert(!marble_arguments_ok("bootcpu=7 epoch=1577836799"));
    assert(!marble_arguments_ok("bootcpu=7 epoch=4102444800"));
    assert(!marble_arguments_ok("bootcpu=7 epoch=999999999999999999999"));
    assert(!marble_arguments_ok("bootcpu=7 epoch=1793070000 epoch=1793070001"));
    assert(marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_cpu_cap=1 marble_efi_idle=1 fb_hw_vsync=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=2 marble_cpu_cap=1 marble_efi_idle=1 fb_hw_vsync=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_brightness=1 marble_cpu_cap=1 marble_efi_idle=1 fb_hw_vsync=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_cpu_cap=1 marble_efi_idle=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_cpu_cap=1 fb_hw_vsync=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_efi_idle=1 fb_hw_vsync=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_brightness=1 marble_cpu_cap=1 marble_efi_idle=1 fb_hw_vsync=1 fb_blank=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 rec_mb=192 rec_step=8 rec_secs=24 rec_start=25 rec_ring=1"));
    assert(!marble_arguments_ok("bootcpu=7 rec_mb=640 rec_step=8"));
    const char *preview="bootcpu=7 cpus=4 marble_gpu_preview=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=90";
    assert(marble_arguments_ok(preview));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_preview=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=91"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_preview=1 marble_cpu_cap=1 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=90"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_preview=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=90 fb_blank=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_preview=1 marble_gpu_preview=1 marble_cpu_cap=1 marble_efi_idle=1 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=90"));
    const char *short_profile="bootcpu=7 marble_thermal_short=1 marble_cpu_cap=1 marble_efi_idle=1 fb_blank=1 loader_return_after=18";
    assert(marble_arguments_ok(short_profile));
    assert(!marble_arguments_ok("bootcpu=7 marble_thermal_short=1 marble_cpu_cap=1 marble_efi_idle=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_thermal_short=1 marble_cpu_cap=1 fb_blank=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_thermal_short=1 marble_cpu_cap=1 marble_efi_idle=1 fb_blank=1 loader_return_after=19"));
    assert(!marble_arguments_ok("bootcpu=7 marble_thermal_short=1 marble_cpu_cap=1 marble_efi_idle=1 fb_blank=1 loader_return_after=18 marble_gpu_probe=1"));
    assert(marble_arguments_ok("bootcpu=7 marble_efi_idle=1 marble_cpu_cap=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_efi_idle=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_efi_idle=1 marble_cpu_cap=1 loader_return_after=91"));
    assert(!marble_arguments_ok("bootcpu=7 marble_efi_idle=1 marble_efi_idle=1 marble_cpu_cap=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_efi_idle=2 marble_cpu_cap=1 loader_return_after=18"));
    assert(marble_arguments_ok("bootcpu=7 marble_cpu_cap=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_cpu_cap=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_cpu_cap=1 loader_return_after=91"));
    assert(!marble_arguments_ok("bootcpu=7 marble_cpu_cap=1 marble_cpu_cap=1 loader_return_after=18"));
    assert(!marble_arguments_ok("bootcpu=7 marble_cpu_cap=2 loader_return_after=18"));
    assert(marble_arguments_ok("-v bootcpu=7 cpus=1 ufs=0 ufs_write=0 var_store=0 loader_return_after=30"));
    assert(marble_arguments_ok("bootcpu=7 cpus=2 ufs=0"));
    assert(marble_arguments_ok("bootcpu=4 cpus=4 ufs=0"));
    assert(marble_arguments_ok("bootcpu=7 cpus=4 fb_threads=2 touch_lat=1 cpuload=1000 marble_cpu_probe=1"));
    assert(marble_arguments_ok("bootcpu=7 fb_beat=0 ca_vbl=1 rec_secs=90 rec_start=35 rec_mb=96 rec_step=6 rec_ring=1"));
    assert(!marble_arguments_ok("bootcpu=7 fb_hz=120"));
    assert(!marble_arguments_ok("bootcpu=7 rec_mb=480"));
    assert(!marble_arguments_ok("bootcpu=7 rec_secs=121"));
    assert(!marble_arguments_ok("bootcpu=7 rec_start=121"));
    assert(!marble_arguments_ok("bootcpu=7 cpufreq=1"));
    assert(!marble_arguments_ok("bootcpu=7 fb_threads=8"));
    assert(!marble_arguments_ok("bootcpu=7 cpuload=1"));
    assert(marble_arguments_ok("bootcpu=7 cpus=4 marble_gpu_probe=1"));
    assert(marble_arguments_ok("bootcpu=7 cpus=4 marble_gpu_probe=2"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=3"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=4"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=5"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=6"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=7"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=8"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=9"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=10"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=11"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=12"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=13"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=14"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=15"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=16"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=17"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 loader_return_after=90"));
    assert(marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 loader_return_after=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 loader_return_after=0"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 loader_return_after=91"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 loader_return_after=90 loader_return_after=1"));
    assert(!marble_arguments_ok("bootcpu=7 gl=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 gl=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 gl=1 gl=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_runtime=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=2"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=17 marble_gpu_runtime=1 marble_gpu_runtime=1"));
    assert(!marble_arguments_ok("bootcpu=7 marble_gpu_probe=18"));
    assert(marble_cpu_count("bootcpu=7 cpus=4 gpu=0") == 4);
    assert(marble_cpu_count("bootcpu=7") == 1);
    assert(!marble_arguments_ok("bootcpu=7 cpus=2 cpus=4"));
    assert(!marble_arguments_ok("bootcpu=7 cpus=3"));
    const char *bad[] = {"", "-v", "bootcpu=0", "bootcpu=8", "bootcpu=7 bootcpu=4",
        "bootcpu=7 ufs=2", "bootcpu=7 ufs_write=1", "bootcpu=7 ufs_native=1",
        "bootcpu=7 ufs_shared_pa=0x1000", "bootcpu=7 var_store=1", "bootcpu=7 root_apfs=ufs_darwin",
        "bootcpu=7 gpu=1", "bootcpu=7 cpus=8", "bootcpu=7 wifi=1", "bootcpu=7 cam=5",
        "bootcpu=7 loader_return_after=3601", "bootcpu=7 debug=9999999999999999999999",
        "bootcpu=7 sb_delay=", "bootcpu=7\tufs=0", "bootcpu=7\nufs=0"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) assert(!marble_arguments_ok(bad[i]));
    char longarg[450]; memset(longarg, ' ', sizeof(longarg));
    memcpy(longarg, "bootcpu=7", 9); longarg[449] = 0;
    assert(!marble_arguments_ok(longarg));
    puts("marble policy: accepted RAM profile; rejected 20 unsafe/malformed profiles");
    return 0;
}
