/*
 * Renesase RCar3 SoC emulation
 *
 * Copyright (C) 2020 Wadim Mueller <wadim.mueller@mail.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef RCAR3_H
#define RCAR3_H

#include "qemu/typedefs.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "hw/timer/armv8_mm_generic_counter.h"
#include "hw/timer/renesas_tpu.h"
#include "hw/misc/rcar_prr.h"
#include "hw/misc/rcar_rst.h"
#include "hw/misc/rcar3_clk.h"
#include "hw/misc/rcar3_sysc.h"
#include "hw/misc/renesas-dbsc4.h"
#include "hw/cpu/cluster.h"
#include "hw/sd/rcar3_sdhi.h"
#include "hw/dma/renesas-sys-dmac.h"
#include "target/arm/cpu.h"
#include "qom/object.h"
#include "hw/misc/unimp.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/net/rcar3_eth_avb.h"
#include "hw/misc/renesas_ipmmu.h"

typedef struct RCAR3Class {
  /*< private >*/
  DeviceClass parent_class;
  /*< public >*/
  const char *lsi_name;
  uint8_t prod_id;
  unsigned a57_count;
  unsigned a53_count;
} RCAR3Class;

#define RCAR3_CLASS(klass) \
    OBJECT_CLASS_CHECK(RCAR3Class, (klass), TYPE_RCAR3)
#define RCAR3_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RCAR3Class, (obj), TYPE_RCAR3)


#define TYPE_RCAR3 "rcar3"
#define TYPE_R8A77965 "r8a77965"

OBJECT_DECLARE_SIMPLE_TYPE(RCar3State, RCAR3)

    

typedef enum {
    IPMMU_START = 0,
    IPMMU_VI0 = IPMMU_START,
    IPMMU_VI1,
    IPMMU_VP0,
    IPMMU_VP1,
    IPMMU_VC0,
    IPMMU_VC1,
    IPMMU_PV0,
    IPMMU_PV1,
    IPMMU_PV2,
    IPMMU_PV3,
    IPMMU_IR,
    IPMMU_HC,
    IPMMU_RT,
    IPMMU_MP,
    IPMMU_DS0,
    IPMMU_DS1,
    IPMMU_VIP0,
    IPMMU_VIP1,
    IPMMU_MM,
    IPMMU_NUM,
} RenesasIpmmuType;


#define RCAR3_CA57_NCPUS 4
#define RCAR3_CA53_NCPUS 4

#define RCAR3_NUM_SCIF  6
#define RCAR3_GIC_SPI_NUM 480
#define RCAR3_GIC_CPU_REG_BASE 0xF1020000

#define RCAR3_DBSC4_BASE 0xe6790000
#define RCAR3_GENERIC_COUNTER_BASE 0xe6080000
#define RCAR3_COUNTER_FREQ 8300000
  
#define RCAR3_PRR_BASE 0xfff00044

#define RCAR3_CPG_BASE 0xe6150000
#define RCAR3_TPU_BASE 0xe6e80000  
#define RCAR3_SYSC_BASE 0xe6180000
#define RCAR3_AVB_BASE  0xe6800000

#define RCAR3_RST_BASE 0xe6160000

#define RCAR3_SDHI_NUM 4

#define RCAR3_NUM_DMA_GROUPS (3U)
#define RCAR3_NUM_THERMAL_SENSORS (3U)

#define RCAR3_DMA_GROUP0_BASE 0xe6700000
#define RCAR3_DMA_GROUP1_BASE 0xe7300000
#define RCAR3_DMA_GROUP2_BASE 0xe7310000

#define RCAR3_MFIS_BASE 0xe6260000
#define RCAR3_AXIB_BASE 0xe6780000
#define RCAR3_THS0_BASE 0xe6198000
#define RCAR3_THS1_BASE 0xe61a0000
#define RCAR3_THS2_BASE 0xe61a8000


typedef enum {
  CSI20,
  CSI40,
  CSI41,
  CSI2_NUM,
} csi2_ids;

typedef enum {
  ISP0_CORE,
  ISP0,
  ISP1_CORE,
  ISP1,
  ISP_NUM,
} isp_ids;

struct RCar3Class {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    unsigned a57_count;
    unsigned a53_count;
    hwaddr peri_base; /* Peripheral base address seen by the CPU */
    hwaddr ctrl_base; /* Interrupt controller and mailboxes etc. */
    int clusterid;
};

struct RCar3State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    uint32_t enabled_cpus;
    CPUClusterState a57_cluster;
    CPUClusterState a53_cluster;
    struct {
        ARMCPU core;
    } ca57[RCAR3_CA57_NCPUS];
    struct {
        ARMCPU core;
    } ca53[RCAR3_CA53_NCPUS];

    ARMCPU *boot_cpu;
    GICState gic;
    MemoryRegion gic_regions[10];
    MemoryRegion sram;
    MemoryRegion bootrom_api;
    ARMV8_MM_GCState gen_counter;
    RCarPrrRegisterState prr;
    RCarRstRegisterState rst;
    RCar3SDHIState sdhi[RCAR3_SDHI_NUM];
    RCar3ClkState cpg;
    RCar3SyscState sysc;
    RenesasDBSC4State dbsc;
    EHCISysBusState usb2c[4];
    RenesasIPMMUState ipmmu[IPMMU_NUM];
    //RenesasSysDmacState dma[RCAR3_NUM_DMA_GROUPS];
    RenesasTPUState tpu;
    RCarEthAvbState avb;
    UnimplementedDeviceState dma[RCAR3_NUM_DMA_GROUPS];
    UnimplementedDeviceState mfis;
    UnimplementedDeviceState axib;
    UnimplementedDeviceState ths[RCAR3_NUM_THERMAL_SENSORS];
    
    UnimplementedDeviceState lbsc;
    UnimplementedDeviceState gpu_3dge;
    UnimplementedDeviceState dave_hd;
    UnimplementedDeviceState dcu;
    UnimplementedDeviceState csi2[CSI2_NUM];
    UnimplementedDeviceState isp[ISP_NUM];
    UnimplementedDeviceState vin[16];
    UnimplementedDeviceState imr_lx4[4];
    UnimplementedDeviceState ivdp1c;
    UnimplementedDeviceState ivcp1e;
    UnimplementedDeviceState imp_x5[4];
    UnimplementedDeviceState ocv[2];
    UnimplementedDeviceState slim_imp;
    UnimplementedDeviceState imp_irq_control;
    UnimplementedDeviceState imp_int_ram;
    UnimplementedDeviceState imp_dmac;
    UnimplementedDeviceState vcp4;
    UnimplementedDeviceState vspbc;
    UnimplementedDeviceState vspbd;
    UnimplementedDeviceState vspb;
    UnimplementedDeviceState vspbs;
    UnimplementedDeviceState vspi0;
    UnimplementedDeviceState vspi1;
    UnimplementedDeviceState vspd0;
    UnimplementedDeviceState vspd1;
    UnimplementedDeviceState vspd2;
    UnimplementedDeviceState fdp1;
    UnimplementedDeviceState fcp;
    UnimplementedDeviceState fcpr;
    UnimplementedDeviceState du[4];
    UnimplementedDeviceState cmm[4];
    UnimplementedDeviceState tcon[2];
    UnimplementedDeviceState doc[2];
    UnimplementedDeviceState lvds;
    UnimplementedDeviceState hdmi[3];
    UnimplementedDeviceState ssiu_dmac;
    UnimplementedDeviceState ssiu_dmacpp;
    UnimplementedDeviceState ssi[10];
    UnimplementedDeviceState adg;
    UnimplementedDeviceState adsp;
    UnimplementedDeviceState drif[8];
    UnimplementedDeviceState dab;
    UnimplementedDeviceState scu;
    UnimplementedDeviceState dtcp;
    UnimplementedDeviceState mlbif;
    UnimplementedDeviceState mlm_dmac[7];
    UnimplementedDeviceState mlm_dmacpp[7];
    UnimplementedDeviceState audio_dmac[2];
    UnimplementedDeviceState audio_dmacpp[29];
    UnimplementedDeviceState stbe[2];
    UnimplementedDeviceState gether;
    UnimplementedDeviceState can_if[2];
    UnimplementedDeviceState can_fd_if;
    UnimplementedDeviceState flex_ray;
    UnimplementedDeviceState pcie_root_complex0;
    UnimplementedDeviceState pcie_root_complex1;
    UnimplementedDeviceState pcie_phy[2];
    UnimplementedDeviceState hscif0[3];
    UnimplementedDeviceState hscif1[2];
    UnimplementedDeviceState i2c01[2];
    UnimplementedDeviceState i2c2;
    UnimplementedDeviceState i2c34[2];
    UnimplementedDeviceState i2c56[2];
    UnimplementedDeviceState i2c7;
    UnimplementedDeviceState iic_for_dvfs;
    UnimplementedDeviceState msiof01[2];
    UnimplementedDeviceState msiof23[2];
    UnimplementedDeviceState pwm[7];
    UnimplementedDeviceState ir;
    UnimplementedDeviceState rpc_if;
    UnimplementedDeviceState ts_if[2];
    UnimplementedDeviceState ssp1;
    UnimplementedDeviceState gyro_adc_if;
    UnimplementedDeviceState adc;
    UnimplementedDeviceState speed_pulse_if;
    UnimplementedDeviceState secure_engine;
    UnimplementedDeviceState caip;
    UnimplementedDeviceState life_cycle[2];
    UnimplementedDeviceState icumxa;
    UnimplementedDeviceState crc;
    UnimplementedDeviceState rfso;
    UnimplementedDeviceState rnandc;
    UnimplementedDeviceState sata;
    //  UnimplementedDeviceState usb2c;
    UnimplementedDeviceState hs_usb[2];
    UnimplementedDeviceState usb_dmac01[2];
    UnimplementedDeviceState usb_dmac23[2];
    UnimplementedDeviceState usb3c;
    UnimplementedDeviceState rwdt;
    UnimplementedDeviceState wwdt;
    UnimplementedDeviceState swdt;
    UnimplementedDeviceState cmt0;
    UnimplementedDeviceState cmt1;
    UnimplementedDeviceState tmu02;
    UnimplementedDeviceState tmu35;
    UnimplementedDeviceState tmu68;
    UnimplementedDeviceState tmu911;
    UnimplementedDeviceState tmu1214;
    UnimplementedDeviceState scmt;
    UnimplementedDeviceState sucmt;
    UnimplementedDeviceState sim;
    UnimplementedDeviceState fm;
};

#endif /* RCAR3_H */
