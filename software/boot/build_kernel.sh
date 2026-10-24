#!/bin/bash
# Build and install the bus kernel on the Pi 5 (see boot/README.md):
# the Raspberry Pi PREEMPT_RT kernel config plus NO_HZ_FULL and RCU_NOCB_CPU.
#
#   boot/build_kernel.sh [SRC_DIR]     (about an hour; run as a normal user)
#
# Installs /boot/firmware/kernel8_rtnohz.img, its initramfs and modules.
# Does not touch config.txt or cmdline.txt.
set -euo pipefail

BRANCH=rpi-6.18.y
SRC=${1:-$HOME/linux}
LOCALVERSION=-v8-rt-nohzfull
KERNEL_IMG=kernel8_rtnohz.img
INITRAMFS=initramfs8_rtnohz

# The packaged RT kernel's config is the starting point
base=$(ls /boot/config-*-v8-rt 2>/dev/null | sort -V | tail -1)
if [ -z "$base" ]; then
    echo "build_kernel: no /boot/config-*-v8-rt; sudo apt install linux-image-rpi-v8-rt first" >&2
    exit 1
fi

sudo apt-get install -y -q git bc bison flex libssl-dev libelf-dev make
[ -d "$SRC" ] || git clone --depth=1 --branch "$BRANCH" https://github.com/raspberrypi/linux "$SRC"
cd "$SRC"

cp "$base" .config
./scripts/config --set-str LOCALVERSION "$LOCALVERSION" \
    -d NO_HZ_IDLE -e NO_HZ_FULL -e RCU_NOCB_CPU -e CPU_ISOLATION
make olddefconfig
for opt in PREEMPT_RT NO_HZ_FULL RCU_NOCB_CPU CPU_ISOLATION; do
    grep -q "^CONFIG_$opt=y" .config || { echo "build_kernel: CONFIG_$opt did not stick" >&2; exit 1; }
done

make -j"$(getconf _NPROCESSORS_ONLN)" Image modules
rel=$(make -s kernelrelease)

sudo make modules_install
sudo cp arch/arm64/boot/Image "/boot/firmware/$KERNEL_IMG"
sudo cp System.map "/boot/System.map-$rel"
sudo cp .config "/boot/config-$rel"
# Raspberry Pi OS's firmware hook warns "Unsupported initramfs version" for a
# custom release and skips the copy; the initrd itself is still generated
sudo update-initramfs -c -k "$rel" || true
sudo cp "/boot/initrd.img-$rel" "/boot/firmware/$INITRAMFS"

echo "build_kernel: installed $rel as /boot/firmware/$KERNEL_IMG"
echo "Next: kernel=$KERNEL_IMG in config.txt and nohz_full=3 rcu_nocbs=3 in cmdline.txt (boot/README.md)"
