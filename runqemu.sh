#!/bin/sh

build/aarch64-softmmu/qemu-system-aarch64 -M virt -cpu cortex-a53 -kernel /home/uia67865/devel/github-conti/linux/arch/arm64/boot/Image -initrd /home/uia67865/devel/cadge/sandbox/initrd.cpio.gz.u-boot -device pci-testdev -device pci-remote-blk -nographic -append "root=/dev/vda earlycon" -m 2048 -no-reboot 
