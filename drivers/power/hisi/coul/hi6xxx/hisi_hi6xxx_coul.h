#ifndef _HISI_HI6XXX_COUL_H_
#define _HISI_HI6XXX_COUL_H_
/*****************************************************************************************
* Filename:	hisi_hi6xxx_coul.h
*
* Discription:  hi6xxx coulometer driver headfile.
* Copyright: 	(C) 2013 huawei.
*
* revision history:
*
******************************************************************************************/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/hisi/hisi_adc.h>
#include <linux/hisi/pmic_interface.h>
#include <linux/mfd/hisi_pmic.h>
#if defined(CONFIG_HISI_COUL_HI6555V200)
#include "hisi_hi6555v200_coul.h"
#elif defined(CONFIG_HISI_COUL_HI6555V300)
#include "hisi_hi6555v300_coul.h"
#elif defined(CONFIG_HISI_COUL_HI6421V600)
#include "hisi_hi6421v600_coul.h"
#elif defined(CONFIG_HISI_COUL_HI6421V700)
#include "hisi_hi6421v700_coul.h"
#else
#include "hisi_hi6421v800_coul.h"
#endif

#define PMU_ENABLE                                  (1)
#define PMU_DISABLE                                 (0)

#define LOCK                                    (1)
#define UNLOCK                                  (0)

#define R_COUL_MOHM                             10      /* resisitance mohm */
#define INVALID_TEMP                            (-99)
#define COUL_HI6XXX                             (0x36)
#define COUL_HI6421V700                         (0x700)
#define DEFAULT_BATTERY_VOL_2_PERCENT           (3350)
#define DEFAULT_BATTERY_VOL_0_PERCENT           (3150)
#define DEFULT_I_GATE_VALUE                     (5000)  // 5000 mA

u32 hi6xxx_coul_reg_read(u32 reg_addr);
void hi6xxx_coul_reg_write(u32 reg_addr, u32 reg_val);
int hi6xxx_coul_regs_read(u32 reg_addr, void *buf, u32 len);
int hi6xxx_coul_regs_write(u32 reg_addr, const void *buf, u32 len);

#define HI6XXX_REG_READ(regAddr)                hi6xxx_coul_reg_read((u32)(regAddr))
#define HI6XXX_REG_WRITE(regAddr,regval)        hi6xxx_coul_reg_write((u32)(regAddr),(u32)(regval))
#define HI6XXX_REGS_READ(regAddr,buf,size)      hi6xxx_coul_regs_read((u32)(regAddr),(void *)(buf),(u32)(size))
#define HI6XXX_REGS_WRITE(regAddr,buf,size)     hi6xxx_coul_regs_write((u32)(regAddr),(const void *)(buf),(u32)(size))
#define HI6XXX_COUL_INFO
#ifndef HI6XXX_COUL_INFO
#define HI6XXX_COUL_ERR(fmt,args...)              do {} while (0)
#define HI6XXX_COUL_EVT(fmt,args...)              do {} while (0)
#define HI6XXX_COUL_INF(fmt,args...)              do {} while (0)
#define HI6XXX_COUL_DBG(fmt,args...)              do {} while (0)
#else
#define HI6XXX_COUL_ERR(fmt,args...) do { printk(KERN_ERR    "[hisi_hi6xxx_coul]" fmt, ## args); } while (0)
#define HI6XXX_COUL_EVT(fmt,args...) do { printk(KERN_WARNING"[hisi_hi6xxx_coul]" fmt, ## args); } while (0)
#define HI6XXX_COUL_INF(fmt,args...) do { printk(KERN_INFO   "[hisi_hi6xxx_coul]" fmt, ## args); } while (0)
#define HI6XXX_COUL_DBG(fmt,args...) /*do { printk(KERN_DEBUG   "[hisi_hi6xxx_coul]" fmt, ## args); } while (0)*/
#endif

struct hi6xxx_coul_device_info
{
    struct device *dev;
    struct delayed_work irq_work;
    int irq;
    unsigned char irq_mask;
    u16 chip_proj;
    u16 chip_version;
};
#endif
