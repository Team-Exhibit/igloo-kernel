/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_MOP500_H
#define __BOARD_MOP500_H

/* This defines the NOMADIK_NR_GPIO */
#include <mach/gpio.h>

/* snowball GPIO for MMC card */
#define SNOWBALL_SDMMC_EN_GPIO 217
#define SNOWBALL_SDMMC_1V8_3V_GPIO 228
#define SNOWBALL_SDMMC_CD_GPIO 218

/* HREFv60-specific GPIO assignments, this board has no GPIO expander */
#define HREFV60_TOUCH_RST_GPIO		143
#define HREFV60_PROX_SENSE_GPIO		217
#define HREFV60_HAL_SW_GPIO		145
#define HREFV60_SDMMC_EN_GPIO		169
#define HREFV60_SDMMC_1V8_3V_GPIO	5
#define HREFV60_SDMMC_CD_GPIO		95
#define HREFV60_ACCEL_INT1_GPIO		82
#define HREFV60_ACCEL_INT2_GPIO		83
#define HREFV60_MAGNET_DRDY_GPIO	32
#define HREFV60_DISP1_RST_GPIO		65
#define HREFV60_DISP2_RST_GPIO		66
#define HREFV60_MMIO_XENON_CHARGE 	170
#define HREFV60_XSHUTDOWN_SECONDARY_SENSOR 140
#define XSHUTDOWN_PRIMARY_SENSOR 	141
#define XSHUTDOWN_SECONDARY_SENSOR 	142

/* GPIOs on the TC35892 expander */
#define MOP500_EGPIO(x)			(NOMADIK_NR_GPIO + (x))
#define GPIO_MAGNET_DRDY		MOP500_EGPIO(1)
#define GPIO_SDMMC_CD			MOP500_EGPIO(3)
#define GPIO_MMIO_XENON_CHARGE 		MOP500_EGPIO(5)
#define GPIO_PROX_SENSOR		MOP500_EGPIO(7)
#define GPIO_HAL_SENSOR			MOP500_EGPIO(8)
#define GPIO_ACCEL_INT1			MOP500_EGPIO(10)
#define GPIO_ACCEL_INT2			MOP500_EGPIO(11)
#define GPIO_BU21013_CS			MOP500_EGPIO(13)
#define MOP500_DISP2_RST_GPIO		MOP500_EGPIO(14)
#define MOP500_DISP1_RST_GPIO		MOP500_EGPIO(15)
#define GPIO_SDMMC_EN			MOP500_EGPIO(17)
#define GPIO_SDMMC_1V8_3V_SEL		MOP500_EGPIO(18)
#define MOP500_EGPIO_END		MOP500_EGPIO(24)

/* GPIOs on the AB8500 mixed-signals circuit */
#define MOP500_AB8500_GPIO(x)		(MOP500_EGPIO_END + (x))

/* mop500 AB8500 GPIO */

/*
 * Snowball AB8500 GPIO
 * Documentation starts on pin 1
 * and framework enumerates from 0
 */
#define SNOWBALL_VSMPS2_1V8_GPIO	MOP500_AB8500_GPIO(0)	/* SYSCLKREQ2/GPIO1 */
#define SNOWBALL_PM_GPIO1_GPIO		MOP500_AB8500_GPIO(1)	/* SYSCLKREQ3/GPIO2 */
#define SNOWBALL_WLAN_CLK_REQ_GPIO	MOP500_AB8500_GPIO(2)	/* SYSCLKREQ4/GPIO3 */
#define SNOWBALL_PM_GPIO4_GPIO		MOP500_AB8500_GPIO(3)	/* SYSCLKREQ6/GPIO4 */
#define SNOWBALL_EN_3V6_GPIO		MOP500_AB8500_GPIO(15)	/* PWMOUT3/GPIO16 */
#define SNOWBALL_PME_ETH_GPIO		MOP500_AB8500_GPIO(23)	/* SYSCLKREQ7/GPIO24 */
#define SNOWBALL_EN_3V3_ETH_GPIO	MOP500_AB8500_GPIO(25)	/* GPIO26 */

struct i2c_board_info;

extern void mop500_sdi_init(void);
extern void mop500_sdi_tc35892_init(void);
void __init mop500_u8500uib_init(void);
void __init mop500_stuib_init(void);
void __init mop500_msp_init(void);
void __init mop500_pins_init(void);

void mop500_uib_i2c_add(int busnum, struct i2c_board_info *info,
		unsigned n);

int msp13_i2s_init(void);
int msp13_i2s_exit(void);

#endif
