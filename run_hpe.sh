#!/bin/sh

make -C build -j20 && \
#build/qemu-system-arm -nographic -d unimp -serial mon:stdio -M hdk11 -kernel /home/uia67865/devel/git/hpe_boot/src/bootloader_s32g/hpe_boot/output/bin/CORTEXM_S32G27X_hpe_boot.elf -s -S
#    build/qemu-system-arm -chardev stdio,mux=on,id=char0 -mon chardev=char0,mode=readline -serial chardev:char0 -nographic -M hdk11 -kernel /home/uia67865/devel/git/hpe_boot/src/bootloader_s32g/hpe_boot/output/bin/CORTEXM_S32G27X_hpe_boot.elf

build/qemu-system-arm -d unimp -nographic -serial mon:stdio -M hdk11 -kernel /home/uia67865/devel/git/hpe_boot/src/bootloader_s32g/hpe_boot/output/bin/CORTEXM_S32G27X_hpe_boot.elf -s

# out_asm,in_asm,op,op_opt,op_ind,op_plugin,int,exec,cpu,fpu,mmu,pcall,cpu_reset,unimp,guest_errors,page,nochain,plugin,strace,vpu
build/qemu-system-arm -trace flexcan_message_box_code_data_frame -object can-bus,id=canbus0 -object can-host-socketcan,id=canhost0,if=vcan0,canbus=canbus0 -machine canbus0=canbus0 -nographic -serial mon:stdio -M hdk11 -kernel /home/uia67865/devel/git/hpe_boot/src/bootloader_s32g/hpe_boot/output/bin/CORTEXM_S32G27X_hpe_boot.elf -s 
