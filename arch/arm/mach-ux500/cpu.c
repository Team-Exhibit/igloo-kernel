/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/clksrc-dbx500-prcmu.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/localtimer.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/reboot_reasons.h>

#include "clock.h"

void __iomem *_PRCMU_BASE;

#ifdef CONFIG_CACHE_L2X0
static void __iomem *l2x0_base;
#endif

void __init ux500_init_devices(void)
{
#ifdef CONFIG_CACHE_L2X0
	BUG_ON(!l2x0_base);

	/*
	 * Unlock Data and Instruction Lock if locked.  This is done here
	 * instead of in l2x0_init since doing it there appears to cause the
	 * second core boot to occasionaly fail.
	 */
	if (readl_relaxed(l2x0_base + L2X0_LOCKDOWN_WAY_D) & 0xFF)
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D);

	if (readl_relaxed(l2x0_base + L2X0_LOCKDOWN_WAY_I) & 0xFF)
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I);
#endif
}

static void ux500_restart(char mode, const char *cmd)
{
	unsigned short reset_code;

	reset_code = reboot_reason_code(cmd);
	prcmu_system_reset(reset_code);

	mdelay(1000);
	printk("Reboot via PRCMU failed -- System halted\n");
	while (1);
}

void __init ux500_init_irq(void)
{
	void __iomem *dist_base;
	void __iomem *cpu_base;

	if (cpu_is_u5500()) {
		dist_base = __io_address(U5500_GIC_DIST_BASE);
		cpu_base = __io_address(U5500_GIC_CPU_BASE);
	} else if (cpu_is_u8500()) {
		dist_base = __io_address(U8500_GIC_DIST_BASE);
		cpu_base = __io_address(U8500_GIC_CPU_BASE);
	} else
		ux500_unknown_soc();

	gic_init(0, 29, dist_base, cpu_base);

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	prcmu_early_init();
	arm_pm_restart = ux500_restart;
	clk_init();
}

#ifdef CONFIG_CACHE_L2X0
static inline void ux500_cache_wait(void __iomem *reg, unsigned long mask)
{
	/* wait for the operation to complete */
	while (readl_relaxed(reg) & mask)
		;
}

static inline void ux500_cache_sync(void)
{
	void __iomem *base = l2x0_base;

	writel_relaxed(0, base + L2X0_CACHE_SYNC);
	ux500_cache_wait(base + L2X0_CACHE_SYNC, 1);
}

/*
 * The L2 cache cannot be turned off in the non-secure world.
 * Dummy until a secure service is in place.
 */
static void ux500_l2x0_disable(void)
{
}

/*
 * This is only called when doing a kexec, just after turning off the L2
 * and L1 cache, and it is surrounded by a spinlock in the generic version.
 * However, we're not really turning off the L2 cache right now and the
 * PL310 does not support exclusive accesses (used to implement the spinlock).
 * So, the invalidation needs to be done without the spinlock.
 */
static void ux500_l2x0_inv_all(void)
{
	void __iomem *base = l2x0_base;
	uint32_t l2x0_way_mask = (1<<16) - 1;	/* Bitmask of active ways */

	/* invalidate all ways */
	writel_relaxed(l2x0_way_mask, base + L2X0_INV_WAY);
	ux500_cache_wait(base + L2X0_INV_WAY, l2x0_way_mask);
	ux500_cache_sync();
}

static int ux500_l2x0_init(void)
{
	if (cpu_is_u5500())
		l2x0_base = __io_address(U5500_L2CC_BASE);
	else if (cpu_is_u8500())
		l2x0_base = __io_address(U8500_L2CC_BASE);
	else
		ux500_unknown_soc();

	/* 64KB way size, 8 way associativity, force WA */
	l2x0_init(l2x0_base, 0x3e060000, 0xc0000fff);

	/* Override invalidate function */
	outer_cache.disable = ux500_l2x0_disable;
	outer_cache.inv_all = ux500_l2x0_inv_all;

	return 0;
}
early_initcall(ux500_l2x0_init);
#endif
