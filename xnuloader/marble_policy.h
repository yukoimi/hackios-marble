/* RAM-only big-core bring-up policy, shared with the host verification. */
#ifndef XNULOADER_MARBLE_POLICY_H
#define XNULOADER_MARBLE_POLICY_H
#include <stddef.h>

static int marble_word_equal(const char *word, size_t size, const char *expected)
{
	size_t i = 0;
	while (i < size && expected[i] && word[i] == expected[i]) i++;
	return i == size && expected[i] == 0;
}
static int marble_has_word(const char *args,const char *expected)
{
    for(const char *p=args;*p;) {
        while(*p==' ')p++;
        const char *word=p;while(*p && *p!=' ')p++;
        if(marble_word_equal(word,(size_t)(p-word),expected))return 1;
    }
    return 0;
}

static int marble_numeric_arg(const char *word, size_t size, const char *prefix, unsigned limit)
{
	size_t i = 0;
	unsigned value = 0;
	while (prefix[i]) {
		if (i >= size || word[i] != prefix[i]) return 0;
		i++;
	}
	if (i == size) return 0;
	for (; i < size; i++) {
		if (word[i] < '0' || word[i] > '9') return 0;
		unsigned digit = (unsigned)(word[i] - '0');
		if (value > limit / 10 || (value == limit / 10 && digit > limit % 10)) return 0;
		value = value * 10 + digit;
	}
	return 1;
}

static int marble_arguments_ok(const char *args)
{
	static const char *const exact[] = {
		"-v", "gl=0", "rd=md0", "touchscreen=0", "buttons=0", "cpufreq=0",
		"gpu=0", "wifi=0", "adsp=0", "ufs=0", "ufs_write=0", "ufs_native=0", "var_store=0",
		"battery=0", "metal=0", "ddr=0", "systrace=1",
		"touch_lat=1", "cpuload=1000", "marble_cpu_probe=1", "marble_idle_probe=1", "fb_blank=1", "fb_threads=1", "fb_threads=2", "fb_threads=4",
        "fb_beat=0", "fb_beat=1", "ca_vbl=1", "fb_hw_vsync=1", "rec_mb=96", "rec_mb=192", "rec_step=6", "rec_step=8", "rec_ring=1",
		"marble_gpu_probe=1", "marble_gpu_probe=2", "marble_gpu_probe=3", "marble_gpu_probe=4", "marble_gpu_probe=5", "marble_gpu_probe=6", "marble_gpu_probe=7", "marble_gpu_probe=8", "marble_gpu_probe=9", "marble_gpu_probe=10", "marble_gpu_probe=11", "marble_gpu_probe=12", "marble_gpu_probe=13", "marble_gpu_probe=14", "marble_gpu_probe=15", "marble_gpu_probe=16", "marble_gpu_probe=17"
	};
	unsigned bootcpu = 0, cpus = 0, runtime = 0, probe17 = 0, gl = 0, budgets = 0, budget = 0, cap = 0, efi_idle = 0;
    unsigned short_thermal=0, blank=0, probes=0, preview=0, brightness=0, epochs=0, scales=0, manual=0, audio_queue=0, audio_transport=0,audio_owner=0,audio_owner8=0,audio_pcm=0,audio_gate=0,audio_refill=0,audio_owner24=0,audio_stream=0;
	size_t total = 0;
	for (const char *p = args; *p;) {
		while (*p == ' ') { p++; total++; }
		if (!*p) break;
		const char *word = p;
		while (*p && *p != ' ') { p++; if (++total > 480) return 0; }
		size_t size = (size_t)(p - word);
		int ok = 0;
        if (marble_word_equal(word,size,"marble_audio_queue=1")) {audio_queue++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_transport=1")) {audio_transport++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_owner=1")) {audio_owner++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_owner_periods=8")) {audio_owner8++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_owner_pcm=1")) {audio_pcm++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_board_gate=1")) {audio_gate++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_refill=1")) {audio_refill++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_owner_periods=24")) {audio_owner24++;ok=1;}
        if (marble_word_equal(word,size,"marble_audio_stream=1")) {audio_stream++;ok=1;}
		if (marble_word_equal(word,size,"marble_cpu_cap=1")) { cap++;ok=1; }
		if (marble_word_equal(word,size,"marble_efi_idle=1")) { efi_idle++;ok=1; }
		if (marble_word_equal(word,size,"marble_thermal_short=1")) { short_thermal++;ok=1; }
		if (marble_word_equal(word,size,"marble_gpu_preview=1")) { preview++;ok=1; }
        if (marble_word_equal(word,size,"marble_gpu_manual=1")) {manual++;ok=1;}
        if (marble_word_equal(word,size,"marble_brightness=1")) {brightness++;ok=1;}
        /* Only the existing 2x logical framebuffer mode, under the complete
         * bounded GPU preview contract. No arbitrary dimensions/scale values. */
        if (marble_word_equal(word,size,"fbscale=2")) {scales++;ok=1;}
        if (marble_numeric_arg(word,size,"epoch=",4102444799u)) {
            unsigned value=0;
            for(size_t i=6;i<size;i++)value=value*10+(unsigned)(word[i]-'0');
            if(value>=1577836800u) {epochs++;ok=1;}
        }
		if (marble_word_equal(word,size,"fb_blank=1")) blank++;
		if (marble_numeric_arg(word,size,"marble_gpu_probe=",17)) probes++;
		if (marble_word_equal(word, size, "marble_gpu_runtime=1")) { runtime++; ok=1; }
		if (marble_word_equal(word, size, "gl=1")) { gl++; ok=1; }
		if (marble_word_equal(word, size, "marble_gpu_probe=17")) probe17++;
		for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
			ok |= marble_word_equal(word, size, exact[i]);
		if (marble_word_equal(word, size, "cpus=1") || marble_word_equal(word, size, "cpus=2") ||
		    marble_word_equal(word, size, "cpus=4")) { cpus++; ok = 1; }
		if (size == 9 && marble_word_equal(word, 8, "bootcpu=") && word[8] >= '4' && word[8] <= '7') {
			bootcpu++; ok = 1;
		}
		if (marble_numeric_arg(word, size, "loader_return_after=", 3600)) {
			budgets++;budget=0;
			for (size_t i=20;i<size;i++) budget=budget*10+(unsigned)(word[i]-'0');
			ok=1;
		}
		ok |= marble_numeric_arg(word, size, "sb_delay=", 120);
		ok |= marble_numeric_arg(word, size, "rec_secs=", 120);
		ok |= marble_numeric_arg(word, size, "rec_start=", 120);
		ok |= marble_numeric_arg(word, size, "debug=", 65535);
		if (!ok) return 0;
	}
	return total <= (preview?480:400) && bootcpu == 1 && cpus <= 1 && runtime <= 1 && gl <= 1 && cap <= 1 && efi_idle <= 1 && epochs<=1 &&
        audio_transport<=1 && (!audio_transport || audio_queue==1) &&
        audio_pcm<=1 && (!audio_pcm || ((audio_owner8==1 || audio_owner24==1) && audio_owner==1)) &&
        audio_gate<=1 && (!audio_gate || (audio_pcm==1 && (audio_owner8==1 || audio_owner24==1) && audio_owner==1)) &&
        audio_refill<=1 && (!audio_refill || (audio_gate==1 && audio_owner8==1 && !audio_stream)) &&
        audio_owner24<=1 && (!audio_owner24 || (audio_stream==1 && !audio_owner8 && audio_owner==1)) &&
        audio_stream<=1 && (!audio_stream || (audio_gate==1 && audio_owner24==1 && !audio_refill)) &&
        audio_owner8<=1 && (!audio_owner8 || audio_owner==1) &&
        audio_owner<=1 && (!audio_owner || (audio_queue==1 && !audio_transport && marble_has_word(args,"cpus=1"))) &&
        audio_queue<=1 && (!audio_queue || (short_thermal==1 && cap==1 && efi_idle==1 && blank==1 && budgets==1 && budget==18 && !preview && !runtime && !gl && !probes && !brightness)) &&
        manual<=1 && (!manual || (preview==1 && budgets==1 && budget==600 &&
            marble_has_word(args,"cpus=4") && marble_has_word(args,"fb_hw_vsync=1"))) &&
        scales<=1 && (!scales || (preview==1 && marble_has_word(args,"fb_hw_vsync=1"))) &&
        brightness<=1 && (!brightness || (cap==1 && efi_idle==1 && marble_has_word(args,"fb_hw_vsync=1") && !short_thermal && !blank)) &&
        preview <= 1 && (!preview || (!short_thermal && !blank && cap==1 && efi_idle==1 && runtime==1 && gl==1 && probe17==1 && probes==1 && budgets==1 && budget>=30 && budget<=(manual?600:90))) &&
        short_thermal <= 1 && (!short_thermal || (blank==1 && !probes && !runtime && !gl && cap==1 && efi_idle==1 && budgets==1 && budget==18)) &&
        (!efi_idle || (cap == 1 && budgets == 1 && budget >= 1 && budget <= (manual?600:90))) &&
        (!cap || (budgets == 1 && budget >= 1 && budget <= (manual?600:90))) &&
        (!runtime || (probe17 == 1 && budgets == 1 && budget >= 1 && budget <= (manual?600:90))) &&
        (!gl || (runtime == 1 && probe17 == 1));
}

static unsigned marble_cpu_count(const char *args)
{
	for (const char *p = args; *p; p++) {
		if ((p == args || p[-1] == ' ') && p[0] == 'c' && p[1] == 'p' && p[2] == 'u' &&
		    p[3] == 's' && p[4] == '=') return (unsigned)(p[5] - '0');
	}
	return 1;
}
#endif
