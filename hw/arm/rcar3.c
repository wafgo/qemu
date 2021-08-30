/*
 * Copyright (c) 2020 Wadim Mueller <wadim.mueller@mail.de>
 *
 * rcar_gen3 SoC emulation.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/intc/arm_gic_common.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "kvm_arm.h"
#include "hw/arm/rcar3.h"
#include "qom/object.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/sh4/sh.h"

#define ARM_PHYS_TIMER_PPI  30
#define ARM_VIRT_TIMER_PPI  27
#define ARM_HYP_TIMER_PPI   26
#define ARM_SEC_TIMER_PPI   29
#define GIC_MAINTENANCE_PPI 25

#define RCAR3_EHCI0_BASE 0xee080000
#define RCAR3_EHCI1_BASE 0xee0a0000
#define RCAR3_EHCI2_BASE 0xee0c0000
#define RCAR3_EHCI3_BASE 0xee0e0000

#define RCAR3_CLASS(klass) \
    OBJECT_CLASS_CHECK(RCAR3Class, (klass), TYPE_RCAR3)
#define RCAR3_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RCAR3Class, (obj), TYPE_RCAR3)

/* 12A.2 HW User Manual of RCar3 */
static struct RCar3GICRegion {
  const char *name;
  int region_index;
  uint32_t address;
  uint32_t offset;
  bool virt;
} rcar3_gic_regions[] = {
  {.name = "Distributor", .region_index = 0, .address = 0xf1010000, .offset = 0, .virt = false},
  {.name = "CPU Interface0", .region_index = 1, .address = 0xf1020000, .offset = 0, .virt = false},
  {.name = "CPU Interface1", .region_index = 1, .address = 0xf1030000, .offset = 0x1000, .virt = false},
};

static struct RCar3IPMMUMap {
    const char *name;
    hwaddr base;
} ipmmu_map[IPMMU_NUM] = {
/*IPMMU_VI0  */ {.name = "IPMMU_VI0", .base = 0xfebd0000},
/*IPMMU_VI1, */ {.name = "IPMMU_VI1", .base = 0xfebe0000},
/*IPMMU_VP0, */ {.name = "IPMMU_VP0", .base = 0xfe990000},
/*IPMMU_VP1, */ {.name = "IPMMU_VP1", .base = 0xfe980000},
/*IPMMU_VC0, */ {.name = "IPMMU_VC0", .base = 0x00000000},
/*IPMMU_VC1, */ {.name = "IPMMU_VC1", .base = 0xfe6f0000},
/*IPMMU_PV0, */ {.name = "IPMMU_PV0", .base = 0xfd800000},
/*IPMMU_PV1, */ {.name = "IPMMU_PV1", .base = 0xfd950000},
/*IPMMU_PV2, */ {.name = "IPMMU_PV2", .base = 0xfd960000},
/*IPMMU_PV3, */ {.name = "IPMMU_PV3", .base = 0xfd970000},
/*IPMMU_IR, */ {.name = "IPMMU_IR", .base = 0xff8b0000},
/*IPMMU_HC, */ {.name = "IPMMU_HC", .base = 0xe6570000},
/*IPMMU_RT, */ {.name = "IPMMU_RT", .base = 0xffc80000},
/*IPMMU_MP, */ {.name = "IPMMU_MP", .base = 0xec670000},
/*IPMMU_DS0, */ {.name = "IPMMU_DS0", .base = 0xe6740000},
/*IPMMU_DS1, */ {.name = "IPMMU_DS1", .base = 0xe7740000},
/*IPMMU_VIP0, */ {.name = "IPMMU_VIP0", .base = 0xe7b00000},
/*IPMMU_VIP1, */ {.name = "IPMMU_VIP1", .base = 0xe7960000},
/*IPMMU_MM, */ {.name = "IPMMU_MM", .base = 0xe67b0000}
};

/* Immitate rcar3 bootrom */
static const char *load_first_stage_ipl(const char *bp1, uint32_t *start_addr)
{
    unsigned long rb, wb;
    static const char *ipl_file_name = "ipl_first_stage.bin";
    FILE *ba1_fl = fopen(bp1, "r");
    if (ba1_fl == NULL) {
        printf("Unable to open bootpartition 1 for extracting IPL first stage\n");
        abort();
    }
    FILE *ipl_first_stage = fopen(ipl_file_name, "w");
    if (ipl_first_stage == NULL) {
        printf("Unable to open file for ibl first stage extraction\n");
        abort();
    }
    fseek(ba1_fl, 0x1d4, SEEK_SET);

    rb = fread(start_addr, 1, 4, ba1_fl);
    if (rb != 4) {
        printf("Unable to read IBL address at offset 0x1d4 (ret = %lu)\n", rb);
        abort();
    }
    fseek(ba1_fl, *start_addr - 0xe6300400, SEEK_SET);
    void *ipl_fs_content = malloc(384 * 1024);
    if (ipl_fs_content == NULL) {
        printf("Unable to alloc memory for IPL extraction\n");
        abort();
    }
    rb = fread(ipl_fs_content, 1, 384 * 1024, ba1_fl);
    if (rb != (384 * 1024)) {
        printf("Unable to read IPL address at offset 0x%x\n", *start_addr - 0xe6300400);
        abort();
    }
    wb = fwrite(ipl_fs_content, 1, 384 * 1024, ipl_first_stage);
    if (wb != (384 * 1024)) {
        printf("Unable to write IBL binary to %s 0x%x\n", ipl_file_name, *start_addr - 0xe6300400);
        abort();
    }
    fclose(ipl_first_stage);
    fclose(ba1_fl);
    free(ipl_fs_content);
    return ipl_file_name;
}

static void rcar3_init(Object *obj)
{
    RCAR3Class *bc = RCAR3_GET_CLASS(obj);
    RCar3State *s = RCAR3(obj);
    int i;

    if (bc->a57_count > 0) {
      object_initialize_child(obj, "a57-cluster", &s->a57_cluster,
                              TYPE_CPU_CLUSTER);
      qdev_prop_set_uint32(DEVICE(&s->a57_cluster), "cluster-id", 0);
      
      for (i = 0; i < bc->a57_count; i++) {
        object_initialize_child(OBJECT(&s->a57_cluster), "a57-cpu[*]",
                                &s->ca57[i].core,
                                ARM_CPU_TYPE_NAME("cortex-a57"));
      }
      
    }
    if (bc->a53_count > 0) {
       object_initialize_child(obj, "a53-cluster", &s->a53_cluster, TYPE_CPU_CLUSTER);
       qdev_prop_set_uint32(DEVICE(&s->a53_cluster), "cluster-id", 1);
       for (i = 0; i < bc->a53_count; i++) {
	 object_initialize_child(OBJECT(&s->a53_cluster), "a53-cpu[*]", &s->ca53[i].core, ARM_CPU_TYPE_NAME("cortex-a53"));
      }
    }


    object_initialize_child(obj, "cpg", &s->cpg, TYPE_RCAR3_CLK);
    object_initialize_child(obj, "gic", &s->gic, gic_class_name());
    object_initialize_child(obj, "generic_counter", &s->gen_counter, TYPE_ARMV8_MM_GC);
    object_initialize_child(obj, "prr", &s->prr, TYPE_RCAR_PRR);
    object_initialize_child(obj, "rst", &s->rst, TYPE_RCAR_RST);
    object_initialize_child(obj, "dbsc", &s->dbsc, TYPE_RENESAS_DBSC4);
    object_initialize_child(obj, "sysc", &s->sysc, TYPE_RCAR3_SYSC);
    object_initialize_child(obj, "tpu", &s->tpu, TYPE_RENESAS_TPU);
    object_initialize_child(obj, "avb", &s->avb, TYPE_RCAR_ETH_AVB);
    
    for (i = 0; i < ARRAY_SIZE(s->usb2c); ++i)
      object_initialize_child(obj, "usb2c[*]", &s->usb2c[i], TYPE_RCAR3_EHCI);

    for (i = 0; i < RCAR3_SDHI_NUM; ++i) {
      object_initialize_child(obj, "sdhi[*]", &s->sdhi[i], TYPE_RCAR_SDHI);
    }

    for (i = IPMMU_START; i < IPMMU_NUM; ++i) {
        struct RCar3IPMMUMap *ipmmu_cfg = &ipmmu_map[i];
        if (ipmmu_cfg->base == 0UL)
            continue;
        object_initialize_child(obj, "ipmmu[*]", &s->ipmmu[i], TYPE_RENESAS_IPMMU);
    }
    /* for (i = 0; i < RCAR3_NUM_DMA_GROUPS; ++i) */
    /*   object_initialize_child(obj, "sys-dmac[*]", &s->dma[i], TYPE_RENESAS_SYSDMAC); */
}

static inline int arm_gic_ppi_index(int cpu_nr, int ppi_index)
{
    return RCAR3_GIC_SPI_NUM + cpu_nr * GIC_INTERNAL + ppi_index;
}



static void rcar3_unimp_area(RCar3State *s, UnimplementedDeviceState *uds, const char *name,
			     hwaddr base, hwaddr size)
{
  object_initialize_child(OBJECT(s), name, uds, TYPE_UNIMPLEMENTED_DEVICE);

  qdev_prop_set_string(DEVICE(uds), "name", name);
  qdev_prop_set_uint64(DEVICE(uds), "size", size);
  sysbus_realize_and_unref(SYS_BUS_DEVICE(uds), &error_fatal);
  sysbus_mmio_map(SYS_BUS_DEVICE(uds), 0, base);
}

#define MK_ITERATABLE_ENTRY(_ds, _name, _base, _size)                          \
  {                                                                            \
    .ds = _ds, .name = _name, .base = _base, .size = _size,                    \
    .iteratable = true, .num_elements = ARRAY_SIZE(_ds)                        \
  }

#define MK_NON_ITERATABLE_ENTRY(_ds, _name, _base, _size)                          \
  {                                                                            \
    .ds = _ds, .name = _name, .base = _base, .size = _size,                    \
    .iteratable = false, .num_elements = 0                        \
  }

static void rcar3_create_unimplemented_devices(RCar3State *s)
{
    struct unmimpl_device_config {
        UnimplementedDeviceState *ds;
        const char *name;
        hwaddr base;
        hwaddr size;
        bool iteratable;
        uint32_t num_elements;
    } all_unimpl_devs[] = {
        MK_NON_ITERATABLE_ENTRY(&s->mfis, "mfis", RCAR3_MFIS_BASE, 0x20000),
        MK_NON_ITERATABLE_ENTRY(&s->axib, "axi-bus", RCAR3_AXIB_BASE, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->dma[0], "sys-dmac[0]", RCAR3_DMA_GROUP0_BASE, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->dma[1], "sys-dmac[1]", RCAR3_DMA_GROUP1_BASE, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->dma[2], "sym-dmac[2]", RCAR3_DMA_GROUP2_BASE, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->ths[0], "thermal-sensor[0]", RCAR3_THS0_BASE, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->ths[1], "thermal-sensor[1]", RCAR3_THS1_BASE, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->ths[2], "thermal-sensor[2]", RCAR3_THS2_BASE, 0x8000),
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VI0], "ipmmu_vi0", 0xfebd0000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VI1], "ipmmu_vi1", 0xfebe0000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VP0], "ipmmu_vp0", 0xfe990000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VP1], "ipmmu_vp1", 0xfe980000, 0x10000), */
        /* /\* {.ds = &s->ipmmu[IPMMU_VC0], .name = "ipmmu_vc0", .base = 0xfe6f0000, 0x10000}, *\/ */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VC1], "ipmmu_vc1", 0xfe6f0000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_PV0], "ipmmu_pv0", 0xfd800000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_PV1], "ipmmu_pv1", 0xfd950000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_PV2], "ipmmu_pv2", 0xfd960000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_PV3], "ipmmu_pv3", 0xfd970000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_IR], "ipmmu_ir", 0xff8b0000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_HC], "ipmmu_hc", 0xe6570000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_RT], "ipmmu_rt", 0xff8c0000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_MP], "ipmmu_mp", 0xec670000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_DS0], "ipmmu_ds0", 0xe6740000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_DS1], "ipmmu_ds1", 0xe7740000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VIP0], "ipmmu_vip0", 0xe7b00000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_VIP1], "ipmmu_vio1", 0xe7960000, 0x10000), */
        /* MK_NON_ITERATABLE_ENTRY(&s->ipmmu[IPMMU_MM], "ipmmu_mm", 0xe67b0000, 0x10000), */
        MK_NON_ITERATABLE_ENTRY(&s->lbsc, "lbsc", 0xee220000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->gpu_3dge, "gpu", 0xfd000000, 0x30000),
        MK_NON_ITERATABLE_ENTRY(&s->dave_hd, "dave-hd", 0xe7900000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->csi2[CSI20], "csi20", 0xfea80000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->csi2[CSI40], "csi40", 0xfeaa0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->csi2[CSI41], "csi41", 0xfeab0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->isp[ISP0_CORE], "isp0_core", 0xfec00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->isp[ISP1_CORE], "isp1_core", 0xfee00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->isp[ISP0], "isp0", 0xfed00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->isp[ISP1], "isp1", 0xfed20000, 0x10000),
        MK_ITERATABLE_ENTRY(s->vin, "vin", 0xe6ef0000, 0x1000),
        MK_ITERATABLE_ENTRY(s->imr_lx4, "imr-lx4", 0xfe860000, 0x10000),
        MK_ITERATABLE_ENTRY(s->imp_x5, "imp-x5", 0xff900000, 0x20000),
        MK_ITERATABLE_ENTRY(s->ocv, "imp-ocv", 0xff980000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->slim_imp, "slim-imp", 0xff9c0000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->imp_irq_control, "slim-irq", 0xffa00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->imp_int_ram, "imp-int-ram", 0xffa40000, 0x20000),
        MK_NON_ITERATABLE_ENTRY(&s->imp_dmac, "imp-dmac", 0xffa10000, 0x4000),
        MK_NON_ITERATABLE_ENTRY(&s->vspbc, "vspbc", 0xfe920000, 0x8000),
        /* {.ds = &s->vspbd, .name = "vspbd", .base = 0xfe960000, 0x8000}, */
        MK_NON_ITERATABLE_ENTRY(&s->vspb, "vspb", 0xfe960000, 0x8000),
        /* {.ds = &s->vspbs, .name = "vspbs", .base = 0xfe920000, 0x8000}, */
        MK_NON_ITERATABLE_ENTRY(&s->vspi0, "vspi0", 0xfe9a0000, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->vspi1, "vspi1", 0xfe9b0000, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->vspd0, "vspd0", 0xfea20000, 0x7000),
        MK_NON_ITERATABLE_ENTRY(&s->vspd1, "vspd1", 0xfea28000, 0x7000),
        MK_NON_ITERATABLE_ENTRY(&s->vspd2, "vspd2", 0xfea30000, 0x7000),
        MK_NON_ITERATABLE_ENTRY(&s->du[0], "du0", 0xfeb00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->du[1], "du1", 0xfeb30000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->du[2], "du2", 0xfeb40000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->du[3], "du3", 0xfeb70000, 0x10000),
        MK_ITERATABLE_ENTRY(s->cmm, "cmm", 0xfea40000, 0x10000),
        MK_ITERATABLE_ENTRY(s->tcon, "tcon", 0xfeb84000, 0x1000),
        MK_ITERATABLE_ENTRY(s->doc, "doc", 0xfeba0000, 0x18000),
        MK_NON_ITERATABLE_ENTRY(&s->lvds, "lvds", 0xfeb90000, 0x200),
        MK_ITERATABLE_ENTRY(s->hdmi, "hdmi", 0xfead0000, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->ssiu_dmac, "ssiu-dmac", 0xec100000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->ssiu_dmacpp, "ssiu-dmapp", 0xec400000, 0x10000),
        MK_ITERATABLE_ENTRY(s->ssi, "ssi", 0xec541000, 0x40),
        MK_NON_ITERATABLE_ENTRY(&s->adg, "adg", 0xec5a0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->adsp, "adsp", 0xec800000, 0x10000),
        MK_ITERATABLE_ENTRY(s->drif, "drif", 0xf6f40000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->dab, "dab", 0xe6730000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->scu, "scu", 0xec500000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->mlbif, "dtcp", 0xec520000, 0x1000),
        MK_ITERATABLE_ENTRY(s->mlm_dmac, "mlm_dmac", 0xec020000, 0x400),
        MK_ITERATABLE_ENTRY(s->mlm_dmacpp, "mlm_dmacpp", 0xec320000, 0x400),
        MK_ITERATABLE_ENTRY(s->audio_dmac, "audio_dmac", 0xec700000, 0x20000),
        MK_ITERATABLE_ENTRY(s->audio_dmacpp, "audio_dmacpp", 0xec740000, 0x10),
        MK_ITERATABLE_ENTRY(s->stbe, "stream-buffer-for-eth-avb", 0xe6a00000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->gether, "gether", 0xe7400000, 0x1000),
        MK_ITERATABLE_ENTRY(s->can_if, "can", 0xe6c30000, 0x800),
        MK_NON_ITERATABLE_ENTRY(&s->can_fd_if, "can-fd", 0xe66c0000, 0x2000),
        MK_NON_ITERATABLE_ENTRY(&s->flex_ray, "flexray", 0xe6b00000, 0x2000),
        MK_NON_ITERATABLE_ENTRY(&s->pcie_root_complex0, "pcie0", 0xfe000000, 0x2000),
        MK_NON_ITERATABLE_ENTRY(&s->pcie_root_complex1, "pcie1", 0xee800000, 0x2000),
        MK_ITERATABLE_ENTRY(s->pcie_phy, "pcie-phy", 0xe65d0000, 0x8000),
        MK_ITERATABLE_ENTRY(s->hscif0, "hscif02", 0xe6540000, 0x10000),
        MK_ITERATABLE_ENTRY(s->hscif1, "hscif34", 0xe66a0000, 0x10000),
        MK_ITERATABLE_ENTRY(s->i2c01, "i2c01", 0xe6500000, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->i2c2, "i2c2", 0xe6508000, 0x1000),
        MK_ITERATABLE_ENTRY(s->i2c34, "i2c34", 0xe66d0000, 0x8000),
        MK_ITERATABLE_ENTRY(s->i2c56, "i2c56", 0xe66e0000, 0x8000),
        MK_NON_ITERATABLE_ENTRY(&s->i2c7, "i2c7", 0xe6690000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->iic_for_dvfs, "i2c_for_dvfs", 0xe60b0000, 0x1000),
        MK_ITERATABLE_ENTRY(s->msiof01, "msiof01", 0xe6e90000, 0x10000),
        MK_ITERATABLE_ENTRY(s->msiof23, "msiof23", 0xe6c00000, 0x10000),
        MK_ITERATABLE_ENTRY(s->pwm, "pwm", 0xe6e30000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->ir, "ir", 0xe6e50000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->rpc_if, "spi-multi-io-buf-controller", 0xee200000, 0x1000),
        MK_ITERATABLE_ENTRY(s->ts_if, "ts-if", 0xe7370000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->gyro_adc_if, "gyro-adc-if", 0xe6e54000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->adc, "adc", 0xffce0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->speed_pulse_if, "speed-pulse-if", 0xe6e55000, 0x1000),
        MK_ITERATABLE_ENTRY(s->life_cycle, "life-cycle", 0xe6110000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->rnandc, "raw-nand-contoller", 0xee180000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->sata, "sata", 0xee300000, 0x200000),
        MK_ITERATABLE_ENTRY(s->hs_usb, "hs-usb", 0xe6590000, 0xc000),
        MK_ITERATABLE_ENTRY(s->usb_dmac01, "usb-dmac01", 0xe65a0000, 0x10000),
        MK_ITERATABLE_ENTRY(s->usb_dmac23, "usb-dmac23", 0xe6460000, 0x10000),
        MK_NON_ITERATABLE_ENTRY(&s->rwdt, "rwdt", 0xe6020000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->wwdt, "wwdt", 0xffc90000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->swdt, "swdt", 0xe6030000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->cmt0, "cmt0", 0xe60f0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->cmt1, "cmt1", 0xe6130000, 0x20000),
        MK_NON_ITERATABLE_ENTRY(&s->tmu02, "tmu02", 0xe61e0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->tmu35, "tmu35", 0xe61fc000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->tmu68, "tmu68", 0xe61fd000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->tmu911, "tmu911", 0xe61fe000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->tmu1214, "tmu1214", 0xffc00000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->scmt, "system-timer", 0xe6040000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->sucmt, "system-up-time-clock", 0xe61d0000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->sim, "sim", 0xe6e56000, 0x1000),
        MK_NON_ITERATABLE_ENTRY(&s->fm, "fm", 0xe6e40000, 0x1000),
    };
  
    for (int i = 0; i < ARRAY_SIZE(all_unimpl_devs); ++i) {
        struct unmimpl_device_config *dev = &all_unimpl_devs[i];
        if (dev->iteratable) {
            for (int j = 0; j < dev->num_elements; ++j) {
                UnimplementedDeviceState *idev = &dev->ds[j];
                char* nm = g_new0(char, dev->name ? (strlen(dev->name) + 3) : 32);
                snprintf(nm, strlen(dev->name) + 3, "%s%i", dev->name, j);
                rcar3_unimp_area(s, idev, nm, dev->base + (j * dev->size), dev->size);
            }
        } else {
            rcar3_unimp_area(s, dev->ds, dev->name, dev->base, dev->size);
        }
    }
}

static void rcar3_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    RCAR3Class *bc = RCAR3_GET_CLASS(OBJECT(dev));
    RCar3State *s = RCAR3(dev);
    int num_cpus = MIN(ms->smp.cpus, 2);
    const char *bootrom_binary;
  
    memory_region_init_ram(&s->sram, NULL, "sram", 384 * 1024, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0xe6300000, &s->sram);
    memory_region_init_ram(&s->bootrom_api, NULL, "br-ram", 0x30000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0xeb100000, &s->bootrom_api);
    /* this is neede to be able to debug the bootrom, due to a gdb bug */
    /* memory_region_init_ram(&dummy_ram, NULL, "dummy-ram", 0x30000, &error_fatal); */
    /* memory_region_add_subregion(get_system_memory(), 0xe7910000, &dummy_ram); */
    
    struct rcar3_per_config {
        hwaddr base;
        uint32_t irq;
    };
  
    struct rcar3_per_config scif_config[RCAR3_NUM_SCIF] = {
        {0xe6e60000, 152},
        {0xe6e68000, 153},
        {0xe6e88000, 164},
        {0xe6c50000, 23},
        {0xe6c40000, 16},
        {0xe6f30000, 17}
    };

    struct rcar3_per_config sdhi_config[RCAR3_SDHI_NUM] = {
        {0xee100000, 197},
        {0xee120000, 198},
        {0xee140000, 199},
        {0xee160000, 200},
    };

    if (bc->a57_count > 0) {
        qdev_realize(DEVICE(&s->a57_cluster), NULL, &error_fatal);
    
        for (int i = 0; i < bc->a57_count; i++) {
            const char *name;

            /* object_property_set_int(OBJECT(&s->ca57[i].core), "psci-conduit", */
            /* 			      QEMU_PSCI_CONDUIT_SMC, &error_abort); */

            name = object_get_canonical_path_component(OBJECT(&s->ca57[i].core));
            if (strcmp(name, "a57-cpu[0]")) {
                /* Secondary CPUs start in PSCI powered-down state */
                object_property_set_bool(OBJECT(&s->ca57[i].core),
                                         "start-powered-off", true, &error_abort);
            } else {
                s->boot_cpu = &s->ca57[i].core;
            }

            object_property_set_bool(OBJECT(&s->ca57[i].core), "has_el3", true, NULL);
            object_property_set_bool(OBJECT(&s->ca57[i].core), "has_el2", true, NULL);
            object_property_set_int(OBJECT(&s->ca57[i].core), "reset-cbar", RCAR3_GIC_CPU_REG_BASE, &error_abort);
            //object_property_set_uint(OBJECT(&s->ca57[i].core), "cntfrq", RCAR3_COUNTER_FREQ, &error_abort);
            object_property_set_int(OBJECT(&s->ca57[i].core), "core-count", num_cpus, &error_abort);
            object_property_set_int(OBJECT(&s->ca57[i].core), "rvbar", 0xe6303a00, &error_abort);
            if (!qdev_realize(DEVICE(&s->ca57[i].core), NULL, errp)) {
                return;
            }
        }
    }

    if (bc->a53_count > 0) {
        qdev_realize(DEVICE(&s->a53_cluster), NULL, &error_fatal);
    
        for (int i = 0; i < bc->a53_count; i++) {
            object_property_set_int(OBJECT(&s->ca53[i].core), "psci-conduit",
                                    QEMU_PSCI_CONDUIT_SMC, &error_abort);
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(OBJECT(&s->ca53[i].core),
                                     "start-powered-off", true, &error_abort);
            object_property_set_bool(OBJECT(&s->ca53[i].core), "has_el3", true, NULL);
            object_property_set_bool(OBJECT(&s->ca53[i].core), "has_el2", true, NULL);
            object_property_set_int(OBJECT(&s->ca53[i].core), "reset-cbar", RCAR3_GIC_CPU_REG_BASE, &error_abort);
            object_property_set_uint(OBJECT(&s->ca53[i].core), "cntfrq", RCAR3_COUNTER_FREQ, &error_abort);
            object_property_set_int(OBJECT(&s->ca53[i].core), "core-count", num_cpus, &error_abort);
            object_property_set_int(OBJECT(&s->ca53[i].core), "rvbar", 0xe6303a00, &error_abort);
            if (!qdev_realize(DEVICE(&s->ca53[i].core), NULL, errp)) {
                return;
            }
        }
    }

    qdev_prop_set_uint32(DEVICE(&s->gen_counter), "freq", RCAR3_COUNTER_FREQ);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", RCAR3_GIC_SPI_NUM + GIC_INTERNAL);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", bc->a53_count + bc->a57_count);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", true);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-virtualization-extensions", true);
    
    qdev_prop_set_uint8(DEVICE(&s->prr), "chip-id", bc->prod_id);
    for (int i = IPMMU_START; i < IPMMU_NUM; ++i) {
        RenesasIPMMUState *ipmmu = &s->ipmmu[i];
        struct RCar3IPMMUMap *ipmmu_cfg = &ipmmu_map[i];
        if (ipmmu_cfg->base == 0UL)
            continue;
        object_property_set_str(OBJECT(ipmmu), "ipmmu_type", ipmmu_cfg->name, NULL);
        object_property_set_bool(OBJECT(ipmmu), "is_main", i == IPMMU_MM ? true : false, NULL);
        object_property_set_link(OBJECT(ipmmu), "main_ipmmu", OBJECT(&s->ipmmu[IPMMU_MM]), &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(ipmmu), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(ipmmu), 0, ipmmu_cfg->base);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(rcar3_gic_regions); i++) {
        SysBusDevice *gic = SYS_BUS_DEVICE(&s->gic);
        const struct RCar3GICRegion *r = &rcar3_gic_regions[i];
        MemoryRegion *mr;
        uint32_t addr = r->address;

        mr = sysbus_mmio_get_region(gic, r->region_index);
        memory_region_init_alias(&s->gic_regions[i], OBJECT(s), "rcar3-gic-alias", mr,
                                 r->offset, 0x1000);
        memory_region_add_subregion(get_system_memory(), addr, &s->gic_regions[i]);
    }

    for (int i = 0; i < bc->a57_count; i++) {
        qemu_irq irq;

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(&s->ca57[i].core), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + bc->a57_count,
                           qdev_get_gpio_in(DEVICE(&s->ca57[i].core), ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + bc->a57_count * 2,
                           qdev_get_gpio_in(DEVICE(&s->ca57[i].core), ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + bc->a57_count * 3,
                           qdev_get_gpio_in(DEVICE(&s->ca57[i].core), ARM_CPU_VFIQ));
        irq = qdev_get_gpio_in(DEVICE(&s->gic), arm_gic_ppi_index(i, ARM_PHYS_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->ca57[i].core), GTIMER_PHYS, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_VIRT_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->ca57[i].core), GTIMER_VIRT, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_HYP_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->ca57[i].core), GTIMER_HYP, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_SEC_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->ca57[i].core), GTIMER_SEC, irq);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gen_counter), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gen_counter), 0, RCAR3_GENERIC_COUNTER_BASE);
    
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->prr), errp)) {
        return;
    }

  
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->prr), 0, RCAR3_PRR_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rst), errp)) {
        return;
    }


    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tpu), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tpu), 0, RCAR3_TPU_BASE);
  
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->cpg), 0, RCAR3_CPG_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->cpg), 0, RCAR3_CPG_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpg), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysc), 0, RCAR3_SYSC_BASE);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysc), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rst), 0, RCAR3_RST_BASE);
  
    for (int i = 0; i < RCAR3_NUM_SCIF; i++) {
        qemu_irq scif_irq = qdev_get_gpio_in(DEVICE(&s->gic), scif_config[i].irq);
        Chardev *cdev = NULL;
        if (i == 2) {
            cdev = serial_hd(0);
        }

        sh_serial_init(get_system_memory(), scif_config[i].base,
                       SH_SERIAL_FEAT_SCIF, 0, cdev,
                       scif_irq, scif_irq, scif_irq, scif_irq, scif_irq);
    }

    for (int i = 0; i < RCAR3_SDHI_NUM; i++) {
        struct rcar3_per_config *sdhi_cfg = &sdhi_config[i];
        object_property_set_link(OBJECT(&s->sdhi[i]), "dma-memory", OBJECT(get_system_memory()), &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhi[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhi[i]), 0, sdhi_cfg->base);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhi[i]), 0, qdev_get_gpio_in(DEVICE(&s->gic), sdhi_cfg->irq));
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dbsc), 0, RCAR3_DBSC4_BASE);
    
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dbsc), errp)) {
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(s->usb2c); ++i) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb2c[i]), 0, RCAR3_EHCI0_BASE + (i * 0x20000));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb2c[i]), errp)) {
            return;
        }
    }

    MemoryRegion *ds0_dma_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ipmmu[IPMMU_DS0]), 1);
    object_property_set_link(OBJECT(&s->avb), "dma-memory", OBJECT(ds0_dma_mr), &error_fatal);
    object_property_set_uint(OBJECT(&s->avb), "utlb_idx", 16, &error_fatal);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->avb), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->avb), 0, RCAR3_AVB_BASE);


    /* if (!sysbus_realize(SYS_BUS_DEVICE(&s->dma[0]), errp)) { */
    /*   return; */
    /* } */

    /* if (!sysbus_realize(SYS_BUS_DEVICE(&s->dma[1]), errp)) { */
    /*   return; */
    /* } */

    /* if (!sysbus_realize(SYS_BUS_DEVICE(&s->dma[2]), errp)) { */
    /*   return; */
    /* } */

    /* sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma[0]), 0, RCAR3_DMA_GROUP0_BASE); */
    /* sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma[1]), 0, RCAR3_DMA_GROUP1_BASE); */
    /* sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma[2]), 0, RCAR3_DMA_GROUP2_BASE); */
    rcar3_create_unimplemented_devices(s);
    if (ms->firmware) {
        bootrom_binary = ms->firmware;
    } else {
        bootrom_binary = "../../rcar3-bootrom/rcar3_rom_code.bin";
    }

    load_image_targphys(bootrom_binary, 0xeb100180, 1024 * 1024);
    /* load first stage ibl here */
    const char *ba1_file = qemu_opt_get(drive_get_by_index(IF_SD, 0)->opts, "file");
    if (ba1_file != NULL) {
        uint32_t start_addr;
        const char *ipl_file = load_first_stage_ipl(ba1_file, &start_addr);
        load_image_targphys(ipl_file, start_addr, 384 * 1024);
    } else {
        load_image_targphys( "/home/local/devel/github-conti/bootloader/bl_patched.bin", 0xe6303a00, 384 * 1024);
    }
}

static void rcar3_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = rcar3_realize;
    dc->user_creatable = false;
}

static void r8a77965_class_init(ObjectClass *oc, void *data)
{
  RCAR3Class *bc = RCAR3_CLASS(oc);
  bc->lsi_name = "r8a77965";
  bc->prod_id = RCAR_M3N_PROD_ID;
  bc->a53_count = 0;
  bc->a57_count = 2;
}

static const TypeInfo rcar3_types[] = {
  {
    .name = TYPE_RCAR3,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RCar3State),
    .instance_init = rcar3_init,
    .class_init = rcar3_class_init,
    .class_size     = sizeof(RCAR3Class),
    .abstract       = true,
  }, {
    .name           = TYPE_R8A77965,
    .parent         = TYPE_RCAR3,
    .class_init     = r8a77965_class_init,
  }
};

DEFINE_TYPES(rcar3_types)
