#!/bin/bash
# Source build on the isolated Linux host. Does not talk to or mount a phone.
set -euo pipefail
case "${1:-all}" in setup|kernel|userland|all) ;; *) echo 'usage: marble-linux-build.sh setup|kernel|userland|all' >&2; exit 2 ;; esac
cd "$(dirname "$(readlink -f "$0")")/.."
export THEOS=${THEOS:-$HOME/theos}
export JOBS=${JOBS:-6}
[ ! -f "$HOME/.marble-build-proxy" ] || source "$HOME/.marble-build-proxy"
# Debian installs dsymutil in LLVM's versioned bin directory.
export PATH="$(dirname "$(readlink -f "$(command -v clang)")"):$PATH"
mkdir -p out/linux-build
# Apple's makesyscalls.sh relies on gawk dropping unknown \( escapes; Debian's
# mawk preserves them and emits invalid C in init_sysent.c.
mkdir -p out/linux-build/hostbin
ln -sfn "$(command -v gawk)" out/linux-build/hostbin/awk
ln -sfn "$(command -v python3)" out/linux-build/hostbin/python
export PATH="$PWD/out/linux-build/hostbin:$PATH"
{
    uname -a
    [ ! -f /etc/debian_version ] || cat /etc/debian_version
    clang --version | head -1
    make --version | head -1
    python3 --version
    gawk --version | head -1
    for d in xnu-src/xnu-8796.121.2 xnu-deps/AvailabilityVersions xnu-deps/libdispatch xnu-deps/libpthread; do
        [ ! -d "$d/.git" ] || { printf '%s ' "$d"; git -C "$d" rev-parse HEAD; }
    done
} > out/linux-build/environment.txt
case "${1:-all}" in
setup|all)
    mkdir -p "$THEOS/toolchain" "$THEOS/sdks"
    if [ ! -x "$THEOS/toolchain/linux/iphone/bin/ld" ]; then
        if [ ! -f out/linux-build/toolchain.tar.xz ]; then
            curl --fail -L --retry 3 -o out/linux-build/toolchain.tar.xz \
                https://github.com/L1ghtmann/llvm-project/releases/download/test-210562a/iOSToolchain-x86_64.tar.xz
        fi
        echo 'a72a7a577e2fbe2838b6b5e9c72034fa7d114af96f0e1d4b016f18730ce4056e  out/linux-build/toolchain.tar.xz' | sha256sum -c -
        sha256sum out/linux-build/toolchain.tar.xz > out/linux-build/toolchain.sha256
        tar -xJf out/linux-build/toolchain.tar.xz -C "$THEOS/toolchain"
    fi
    if [ ! -d "$THEOS/sdks/iPhoneOS16.5.sdk/usr/include" ]; then
        git -C "$THEOS/sdks" init
        git -C "$THEOS/sdks" remote add origin https://github.com/theos/sdks.git
        git -C "$THEOS/sdks" sparse-checkout set iPhoneOS16.5.sdk
        git -C "$THEOS/sdks" fetch --depth 1 --filter=blob:none origin 0222fd5413cf4b9af096f37b4621afa2688572f7
        git -C "$THEOS/sdks" checkout --detach FETCH_HEAD
    fi
    python3 tools/marble-source-normalize.py "$THEOS/sdks/iPhoneOS16.5.sdk"
    [ -d xnu-src/xnu-8796.121.2 ] || bash xnu/apply.sh
    bash xnu-deps/setup.sh
    ;;
esac
case "${1:-all}" in
kernel|all)
    # -k permits upstream's export-list checks to finish after linking. Never
    # accept an old artifact just because make's expected final status is 2.
    obj=xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837
    rm -f "$obj/mach.development.bcm2837" "$obj/mach.development.bcm2837.unstripped"
    rc=0
    bash xnu-build.sh -j"$JOBS" -k > out/linux-build/kernel.log 2>&1 || rc=$?
    [ -s "$obj/mach.development.bcm2837" ] || { tail -70 out/linux-build/kernel.log; exit 1; }
    [ "$rc" = 0 ] || [ "$rc" = 2 ] || exit "$rc"
    cp "$obj/mach.development.bcm2837" out/linux-build/mach.marble-source
    python3 tools/marble_source.py out/linux-build/mach.marble-source --build-tree xnu-src/xnu-8796.121.2
    "$THEOS/toolchain/linux/iphone/bin/nm" -n "$obj/mach.development.bcm2837.unstripped" > out/linux-build/kernel.syms
    sha256sum out/linux-build/mach.marble-source
    echo "make exit status: $rc; inspect kernel.log before approving this candidate"
    ;;
esac
case "${1:-all}" in
userland|all)
    source ./xnu-env.sh
    output=$PWD/out/linux-build/userland
    mkdir -p "$output"
    cc=(xnu-clang -arch arm64 -Os -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra)
    "${cc[@]}" -Iuserland/libmini -c userland/libmini/mini.c -o "$output/mini.o"
    "${cc[@]}" -c userland/libmini/start.S -o "$output/start.o"
    "${cc[@]}" -Wl,-e,_start -Wl,-pagezero_size,0x100000000 -o "$output/hello" userland/src/hello.c
    for name in init child stage0; do
        "${cc[@]}" -Iuserland/libmini -Wl,-e,_start -Wl,-pagezero_size,0x100000000 \
            -o "$output/$name" "userland/src/$name.c" "$output/mini.o" "$output/start.o"
    done
    dyn=(xnu-clang -arch arm64 -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" -O1)
    "${dyn[@]}" -o "$output/hello_dyn" userland/src/hello_dyn.c
    "${dyn[@]}" -o "$output/cpu_test" userland/src/cpu_test.c
    "${dyn[@]}" -framework CoreFoundation -o "$output/rwtouch" userland/src/rwtouch.c
    "${dyn[@]}" -o "$output/marble-cpu-stress" tools/marble-cpu-stress.c
    "${dyn[@]}" -framework CoreFoundation -o "$output/svc_test" userland/src/svc_test.c
    "${dyn[@]}" -framework CoreFoundation -framework IOSurface -Wno-four-char-constants -o "$output/surf_test" userland/src/surf_test.c
    "${dyn[@]}" -framework CoreFoundation -lMobileGestalt -o "$output/mg_test" userland/src/mg_test.c
    "${dyn[@]}" -dynamiclib -install_name /redwood/lib/libredwood.dylib \
        -framework CoreFoundation -framework IOKit -framework IOSurface -lobjc \
        -o "$output/libredwood.dylib" userland/src/inject.c
    "${dyn[@]}" -dynamiclib -install_name /redwood/lib/libredwood-sb.dylib \
        -framework CoreFoundation -framework IOKit -lobjc -o "$output/libredwood-sb.dylib" userland/src/inject_sb.c
    "${cc[@]}" -Iuserland/libmini -Wl,-e,_start -Wl,-pagezero_size,0x100000000 \
        -o "$output/marble-touch-bridge" tools/marble-touch-bridge.c "$output/mini.o" "$output/start.o"
    "${dyn[@]}" -dynamiclib -undefined dynamic_lookup -install_name /redwood/lib/marble-input-qos.dylib \
        -o "$output/marble-input-qos.dylib" tools/marble-input-qos.c
    find "$output" -type f ! -name '*.o' -exec sha256sum {} + > out/linux-build/userland.sha256
    ;;
esac
echo "marble Linux build ${1:-all}: finished"
