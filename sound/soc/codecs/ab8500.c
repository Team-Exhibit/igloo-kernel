/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Mikko J. Lehto <mikko.lehto@symbio.com>,
 *         Mikko Sarmanne <mikko.sarmanne@symbio.com>,
 *         Jarmo K. Kuronen <jarmo.kuronen@symbio.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include "ab8500.h"
#include "../ux500/ux500_ab8500.h"

/* To convert register definition shifts to masks */
#define BMASK(bsft)	(1 << (bsft))

/* Macrocell value definitions */
#define CLK_32K_OUT2_DISABLE			0x01
#define INACTIVE_RESET_AUDIO			0x02
#define ENABLE_AUDIO_CLK_TO_AUDIO_BLK		0x10
#define ENABLE_VINTCORE12_SUPPLY		0x04
#define GPIO27_DIR_OUTPUT			0x04
#define GPIO29_DIR_OUTPUT			0x10
#define GPIO31_DIR_OUTPUT			0x40
#define GPIO35_DIR_OUTPUT			0x04

/* Macrocell register definitions */
#define AB8500_CTRL3_REG			0x0200
#define AB8500_SYSULPCLK_CTRL1_REG		0x020B
#define AB8500_GPIO_DIR4_REG			0x1013
#define AB8500_GPIO_DIR5_REG			0x1014
#define AB8500_GPIO_OUT5_REG			0x1024

/*
 * AB8500 register cache & default register settings
 */
static const u8 ab8500_reg_cache[AB8500_CACHEREGNUM] = {
	0x88, /* REG_POWERUP		(0x00) */
	0x00, /* REG_AUDSWRESET		(0x01) */
	0x00, /* REG_ADPATHENA		(0x02) */
	0x00, /* REG_DAPATHENA		(0x03) */
	0x00, /* REG_ANACONF1		(0x04) */
	0x0F, /* REG_ANACONF2		(0x05) */
	0x00, /* REG_DIGMICCONF		(0x06) */
	0x00, /* REG_ANACONF3		(0x07) */
	0x00, /* REG_ANACONF4		(0x08) */
	0x00, /* REG_DAPATHCONF		(0x09) */
	0x40, /* REG_MUTECONF		(0x0A) */
	0x00, /* REG_SHORTCIRCONF	(0x0B) */
	0x01, /* REG_ANACONF5		(0x0C) */
	0x00, /* REG_ENVCPCONF		(0x0D) */
	0x00, /* REG_SIGENVCONF		(0x0E) */
	0x3F, /* REG_PWMGENCONF1	(0x0F) */
	0x32, /* REG_PWMGENCONF2	(0x10) */
	0x32, /* REG_PWMGENCONF3	(0x11) */
	0x32, /* REG_PWMGENCONF4	(0x12) */
	0x32, /* REG_PWMGENCONF5	(0x13) */
	0x0F, /* REG_ANAGAIN1		(0x14) */
	0x0F, /* REG_ANAGAIN2		(0x15) */
	0x22, /* REG_ANAGAIN3		(0x16) */
	0x55, /* REG_ANAGAIN4		(0x17) */
	0x13, /* REG_DIGLINHSLGAIN	(0x18) */
	0x13, /* REG_DIGLINHSRGAIN	(0x19) */
	0x00, /* REG_ADFILTCONF		(0x1A) */
	0x00, /* REG_DIGIFCONF1		(0x1B) */
	0x02, /* REG_DIGIFCONF2		(0x1C) */
	0x00, /* REG_DIGIFCONF3		(0x1D) */
	0x02, /* REG_DIGIFCONF4		(0x1E) */
	0xCC, /* REG_ADSLOTSEL1		(0x1F) */
	0xCC, /* REG_ADSLOTSEL2		(0x20) */
	0xCC, /* REG_ADSLOTSEL3		(0x21) */
	0xCC, /* REG_ADSLOTSEL4		(0x22) */
	0xCC, /* REG_ADSLOTSEL5		(0x23) */
	0xCC, /* REG_ADSLOTSEL6		(0x24) */
	0xCC, /* REG_ADSLOTSEL7		(0x25) */
	0xCC, /* REG_ADSLOTSEL8		(0x26) */
	0xCC, /* REG_ADSLOTSEL9		(0x27) */
	0xCC, /* REG_ADSLOTSEL10	(0x28) */
	0xCC, /* REG_ADSLOTSEL11	(0x29) */
	0xCC, /* REG_ADSLOTSEL12	(0x2A) */
	0xCC, /* REG_ADSLOTSEL13	(0x2B) */
	0xCC, /* REG_ADSLOTSEL14	(0x2C) */
	0xCC, /* REG_ADSLOTSEL15	(0x2D) */
	0xCC, /* REG_ADSLOTSEL16	(0x2E) */
	0x00, /* REG_ADSLOTHIZCTRL1	(0x2F) */
	0x00, /* REG_ADSLOTHIZCTRL2	(0x30) */
	0x00, /* REG_ADSLOTHIZCTRL3	(0x31) */
	0x00, /* REG_ADSLOTHIZCTRL4	(0x32) */
	0x08, /* REG_DASLOTCONF1	(0x33) */
	0x08, /* REG_DASLOTCONF2	(0x34) */
	0x08, /* REG_DASLOTCONF3	(0x35) */
	0x08, /* REG_DASLOTCONF4	(0x36) */
	0x08, /* REG_DASLOTCONF5	(0x37) */
	0x08, /* REG_DASLOTCONF6	(0x38) */
	0x08, /* REG_DASLOTCONF7	(0x39) */
	0x08, /* REG_DASLOTCONF8	(0x3A) */
	0x00, /* REG_CLASSDCONF1	(0x3B) */
	0x00, /* REG_CLASSDCONF2	(0x3C) */
	0x84, /* REG_CLASSDCONF3	(0x3D) */
	0x00, /* REG_DMICFILTCONF	(0x3E) */
	0xFE, /* REG_DIGMULTCONF1	(0x3F) */
	0xC0, /* REG_DIGMULTCONF2	(0x40) */
	0x3F, /* REG_ADDIGGAIN1		(0x41) */
	0x3F, /* REG_ADDIGGAIN2		(0x42) */
	0x1F, /* REG_ADDIGGAIN3		(0x43) */
	0x1F, /* REG_ADDIGGAIN4		(0x44) */
	0x3F, /* REG_ADDIGGAIN5		(0x45) */
	0x3F, /* REG_ADDIGGAIN6		(0x46) */
	0x1F, /* REG_DADIGGAIN1		(0x47) */
	0x1F, /* REG_DADIGGAIN2		(0x48) */
	0x3F, /* REG_DADIGGAIN3		(0x49) */
	0x3F, /* REG_DADIGGAIN4		(0x4A) */
	0x3F, /* REG_DADIGGAIN5		(0x4B) */
	0x3F, /* REG_DADIGGAIN6		(0x4C) */
	0x3F, /* REG_ADDIGLOOPGAIN1	(0x4D) */
	0x3F, /* REG_ADDIGLOOPGAIN2	(0x4E) */
	0x00, /* REG_HSLEARDIGGAIN	(0x4F) */
	0x00, /* REG_HSRDIGGAIN		(0x50) */
	0x1F, /* REG_SIDFIRGAIN1	(0x51) */
	0x1F, /* REG_SIDFIRGAIN2	(0x52) */
	0x00, /* REG_ANCCONF1		(0x53) */
	0x00, /* REG_ANCCONF2		(0x54) */
	0x00, /* REG_ANCCONF3		(0x55) */
	0x00, /* REG_ANCCONF4		(0x56) */
	0x00, /* REG_ANCCONF5		(0x57) */
	0x00, /* REG_ANCCONF6		(0x58) */
	0x00, /* REG_ANCCONF7		(0x59) */
	0x00, /* REG_ANCCONF8		(0x5A) */
	0x00, /* REG_ANCCONF9		(0x5B) */
	0x00, /* REG_ANCCONF10		(0x5C) */
	0x00, /* REG_ANCCONF11		(0x5D) - read only */
	0x00, /* REG_ANCCONF12		(0x5E) - read only */
	0x00, /* REG_ANCCONF13		(0x5F) - read only */
	0x00, /* REG_ANCCONF14		(0x60) - read only */
	0x00, /* REG_SIDFIRADR		(0x61) */
	0x00, /* REG_SIDFIRCOEF1	(0x62) */
	0x00, /* REG_SIDFIRCOEF2	(0x63) */
	0x00, /* REG_SIDFIRCONF		(0x64) */
	0x00, /* REG_AUDINTMASK1	(0x65) */
	0x00, /* REG_AUDINTSOURCE1	(0x66) - read only */
	0x00, /* REG_AUDINTMASK2	(0x67) */
	0x00, /* REG_AUDINTSOURCE2	(0x68) - read only */
	0x00, /* REG_FIFOCONF1		(0x69) */
	0x00, /* REG_FIFOCONF2		(0x6A) */
	0x00, /* REG_FIFOCONF3		(0x6B) */
	0x00, /* REG_FIFOCONF4		(0x6C) */
	0x00, /* REG_FIFOCONF5		(0x6D) */
	0x00, /* REG_FIFOCONF6		(0x6E) */
	0x02, /* REG_AUDREV		(0x6F) - read only */
};

/* Reads an arbitrary register from the ab8500 chip.
*/
static int ab8500_read_reg(struct snd_soc_codec *codec, unsigned int bank,
		unsigned int reg)
{
	u8 value;
	int status = abx500_get_register_interruptible(
		codec->dev, bank, reg, &value);

	if (status < 0) {
		pr_err("%s: Register (%02x:%02x) read failed (%d).\n",
			__func__, (u8)bank, (u8)reg, status);
	} else {
		pr_debug("Read 0x%02x from register %02x:%02x\n",
			(u8)value, (u8)bank, (u8)reg);
		status = value;
	}

	return status;
}

/* Writes an arbitrary register to the ab8500 chip.
 */
static int ab8500_write_reg(struct snd_soc_codec *codec, unsigned int bank,
		unsigned int reg, unsigned int value)
{
	int status = abx500_set_register_interruptible(
		codec->dev, bank, reg, value);

	if (status < 0) {
		pr_err("%s: Register (%02x:%02x) write failed (%d).\n",
			__func__, (u8)bank, (u8)reg, status);
	} else {
		pr_debug("Wrote 0x%02x into register %02x:%02x\n",
			(u8)value, (u8)bank, (u8)reg);
	}

	return status;
}

/* Reads an audio register from the cache.
 */
static unsigned int ab8500_audio_read_reg(struct snd_soc_codec *codec,
		unsigned int reg)
{
	u8 *cache = codec->reg_cache;
	return cache[reg];
}

/* Reads an audio register from the hardware.
 */
static int ab8500_audio_read_reg_nocache(struct snd_soc_codec *codec,
		unsigned int reg)
{
	u8 *cache = codec->reg_cache;
	int value = ab8500_read_reg(codec, AB8500_AUDIO, reg);

	if (value >= 0)
		cache[reg] = value;

	return value;
}

/* Writes an audio register to the hardware and cache.
 */
static int ab8500_audio_write_reg(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	u8 *cache = codec->reg_cache;
	int status = ab8500_write_reg(codec, AB8500_AUDIO, reg, value);

	if (status >= 0)
		cache[reg] = value;

	return status;
}

/* Dumps all audio registers.
 */
static inline void ab8500_audio_dump_all_reg(struct snd_soc_codec *codec)
{
	int i;

	pr_debug("%s Enter.\n", __func__);

	for (i = AB8500_FIRST_REG; i <= AB8500_LAST_REG; i++)
		ab8500_audio_read_reg_nocache(codec, i);
}

/* Updates an audio register.
 */
static inline int ab8500_update_audio_reg(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int clr, unsigned int ins)
{
	unsigned int new, old;

	old = ab8500_audio_read_reg(codec, reg);
	new = (old & ~clr) | ins;
	if (old == new)
		return 0;

	return ab8500_audio_write_reg(codec, reg, new);
}

/*--------------------------------------------------------------*/

/* Whether widget's register definitions should be inverted or not */
enum control_inversion {
	NORMAL = 0,
	INVERT = 1
};

/* HS left channel mute control */
static const struct snd_kcontrol_new dapm_hsl_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_MUTECONF,
			REG_MUTECONF_MUTHSL, 1, INVERT),
};

/* HS right channel mute control */
static const struct snd_kcontrol_new dapm_hsr_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_MUTECONF,
			REG_MUTECONF_MUTHSR, 1, INVERT),
};

/* Earpiece mute control */
static const struct snd_kcontrol_new dapm_ear_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_MUTECONF,
			REG_MUTECONF_MUTEAR, 1, INVERT),
};

/* IHF left channel mute control */
static const struct snd_kcontrol_new dapm_ihfl_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_DATOHFLEN, 1, NORMAL),
};

/* IHF right channel mute control */
static const struct snd_kcontrol_new dapm_ihfr_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_DATOHFREN, 1, NORMAL),
};

/* Mic 1 mute control */
static const struct snd_kcontrol_new dapm_mic1_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2,
			REG_ANACONF2_MUTMIC1, 1, INVERT),
};

/* Mic 2 mute control */
static const struct snd_kcontrol_new dapm_mic2_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2,
			REG_ANACONF2_MUTMIC2, 1, INVERT),
};

/* LineIn left channel mute control */
static const struct snd_kcontrol_new dapm_linl_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2,
			REG_ANACONF2_MUTLINL, 1, INVERT),
};

/* LineIn right channel mute control */
static const struct snd_kcontrol_new dapm_linr_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2,
			REG_ANACONF2_MUTLINR, 1, INVERT),
};

/* DMic 1 mute control */
static const struct snd_kcontrol_new dapm_dmic1_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC1, 1, NORMAL),
};

/* DMic 2 mute control */
static const struct snd_kcontrol_new dapm_dmic2_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC2, 1, NORMAL),
};

/* DMic 3 mute control */
static const struct snd_kcontrol_new dapm_dmic3_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC3, 1, NORMAL),
};

/* DMic 4 mute control */
static const struct snd_kcontrol_new dapm_dmic4_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC4, 1, NORMAL),
};

/* DMic 5 mute control */
static const struct snd_kcontrol_new dapm_dmic5_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC5, 1, NORMAL),
};

/* DMic 6 mute control */
static const struct snd_kcontrol_new dapm_dmic6_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC6, 1, NORMAL),
};

/* ANC to Earpiece mute control */
static const struct snd_kcontrol_new dapm_anc_ear_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_ANCSEL, 1, NORMAL),
};

/* Earpiece source selector control */
static const char *enum_ear_source[] = {"Headset Left", "IHF Left"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ear_source, REG_DMICFILTCONF,
			REG_DMICFILTCONF_DA3TOEAR, enum_ear_source);

static const struct snd_kcontrol_new dapm_ear_source[] = {
	SOC_DAPM_ENUM("Earpiece Source", dapm_enum_ear_source),
};

/* IHF / ANC selector control */
static const char *enum_ihfx_sel[] = {"Audio Path", "ANC"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ihfl_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_HFLSEL, enum_ihfx_sel);

static const struct snd_kcontrol_new dapm_ihfl_select[] = {
	SOC_DAPM_ENUM("IHF Left Source", dapm_enum_ihfl_sel),
};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ihfr_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_HFRSEL, enum_ihfx_sel);

static const struct snd_kcontrol_new dapm_ihfr_select[] = {
	SOC_DAPM_ENUM("IHF Right Source", dapm_enum_ihfr_sel),
};

/* Mic 1A or 1B selector control */
static const char *enum_mic1ab_sel[] = {"Mic 1A", "Mic 1B"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_mic1ab_sel, REG_ANACONF3,
			REG_ANACONF3_MIC1SEL, enum_mic1ab_sel);

static const struct snd_kcontrol_new dapm_mic1ab_select[] = {
	SOC_DAPM_ENUM("Mic 1A or 1B Select", dapm_enum_mic1ab_sel),
};

/* Mic 2 or LineIn Right selector control */
static const char *enum_mic2lr_sel[] = {"Mic 2", "LineIn Right"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_mic2lr_sel, REG_ANACONF3,
			REG_ANACONF3_LINRSEL, enum_mic2lr_sel);

static const struct snd_kcontrol_new dapm_mic2lr_select[] = {
	SOC_DAPM_ENUM("Mic 2 or LINR Select", dapm_enum_mic2lr_sel),
};

/* AD1 selector control */
static const char *enum_ad1_sel[] = {"LineIn Left", "DMic 1"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ad1_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD1SEL, enum_ad1_sel);

static const struct snd_kcontrol_new dapm_ad1_select[] = {
	SOC_DAPM_ENUM("AD 1 Select", dapm_enum_ad1_sel),
};

/* AD2 selector control */
static const char *enum_ad2_sel[] = {"LineIn Right", "DMic 2"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ad2_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD2SEL, enum_ad2_sel);

static const struct snd_kcontrol_new dapm_ad2_select[] = {
	SOC_DAPM_ENUM("AD 2 Select", dapm_enum_ad2_sel),
};

/* AD3 selector control */
static const char *enum_ad3_sel[] = {"Mic 1", "DMic 3"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ad3_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD3SEL, enum_ad3_sel);

static const struct snd_kcontrol_new dapm_ad3_select[] = {
	SOC_DAPM_ENUM("AD 3 Select", dapm_enum_ad3_sel),
};

/* AD5 selector control */
static const char *enum_ad5_sel[] = {"Mic 2", "DMic 5"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ad5_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD5SEL, enum_ad5_sel);

static const struct snd_kcontrol_new dapm_ad5_select[] = {
	SOC_DAPM_ENUM("AD 5 Select", dapm_enum_ad5_sel),
};

/* AD6 selector control */
static const char *enum_ad6_sel[] = {"Mic 1", "DMic 6"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_ad6_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD6SEL, enum_ad6_sel);

static const struct snd_kcontrol_new dapm_ad6_select[] = {
	SOC_DAPM_ENUM("AD 6 Select", dapm_enum_ad6_sel),
};

/* ANC input selector control */
static const char *enum_anc_in_sel[] = {"Mic 1 / DMic 6", "Mic 2 / DMic 5"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_anc_in_sel, REG_DMICFILTCONF,
			REG_DMICFILTCONF_ANCINSEL, enum_anc_in_sel);

static const struct snd_kcontrol_new dapm_anc_in_select[] = {
	SOC_DAPM_ENUM("ANC Source", dapm_enum_anc_in_sel),
};

/* ANC enable control */
static const char *enum_anc_dis_ena[] = {"Disabled", "Enabled"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_anc_enable, REG_ANCCONF1,
			REG_ANCCONF1_ENANC, enum_anc_dis_ena);

static const struct snd_kcontrol_new dapm_anc_enable[] = {
	SOC_DAPM_ENUM("ANC", dapm_enum_anc_enable),
};

/* Sidetone left input selector control */
static const char *enum_stfir1_in_sel[] = {
	"LineIn Left", "LineIn Right", "Mic 1", "Headset Left"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_stfir1_in_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_FIRSID1SEL, enum_stfir1_in_sel);

static const struct snd_kcontrol_new dapm_stfir1_in_select[] = {
	SOC_DAPM_ENUM("Sidetone Left Source", dapm_enum_stfir1_in_sel),
};

/* Sidetone right input selector control */
static const char *enum_stfir2_in_sel[] = {
	"LineIn Right", "Mic 1", "DMic 4", "Headset Right"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_stfir2_in_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_FIRSID2SEL, enum_stfir2_in_sel);

static const struct snd_kcontrol_new dapm_stfir2_in_select[] = {
	SOC_DAPM_ENUM("Sidetone Right Source", dapm_enum_stfir2_in_sel),
};

/* Vibra path selector control */
static const char *enum_pwm2vibx[] = {"Audio Path", "PWM Generator"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_pwm2vib1, REG_PWMGENCONF1,
			REG_PWMGENCONF1_PWMTOVIB1, enum_pwm2vibx);

static const struct snd_kcontrol_new dapm_pwm2vib1[] = {
	SOC_DAPM_ENUM("Vibra 1 Controller", dapm_enum_pwm2vib1),
};

static SOC_ENUM_SINGLE_DECL(dapm_enum_pwm2vib2, REG_PWMGENCONF1,
			REG_PWMGENCONF1_PWMTOVIB2, enum_pwm2vibx);

static const struct snd_kcontrol_new dapm_pwm2vib2[] = {
	SOC_DAPM_ENUM("Vibra 2 Controller", dapm_enum_pwm2vib2),
};

static const struct snd_soc_dapm_widget ab8500_dapm_widgets[] = {
	/* Headset path */

	SND_SOC_DAPM_AIF_IN("DA_IN1", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN2", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),

	/* XXX SwapDA12_34 */

	SND_SOC_DAPM_MIXER("DA1 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA1, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA2 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA2, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("HSL Digital Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR Digital Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("HSL DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHSL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHSR, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("HSL DAC Driver", REG_ANACONF3,
			REG_ANACONF3_ENDRVHSL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR DAC Driver", REG_ANACONF3,
			REG_ANACONF3_ENDRVHSR, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH("Headset Left", SND_SOC_NOPM, 0, 0, dapm_hsl_mute),
	SND_SOC_DAPM_SWITCH("Headset Right", SND_SOC_NOPM, 0, 0, dapm_hsr_mute),

	SND_SOC_DAPM_MIXER("HSL Enable", REG_ANACONF4,
			REG_ANACONF4_ENHSL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR Enable", REG_ANACONF4,
			REG_ANACONF4_ENHSR, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("Charge Pump", REG_ANACONF5,
			REG_ANACONF5_ENCPHS, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HSL"),
	SND_SOC_DAPM_OUTPUT("HSR"),


	/* Earpiece path */

	SND_SOC_DAPM_MUX("Earpiece Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_ear_source),

	SND_SOC_DAPM_MIXER("EAR DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACEAR, 0, NULL, 0),

	SND_SOC_DAPM_SWITCH("Earpiece", SND_SOC_NOPM, 0, 0, dapm_ear_mute),

	SND_SOC_DAPM_MIXER("EAR Enable", REG_ANACONF4,
			REG_ANACONF4_ENEAR, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("EAR"),


	/* Handsfree path */

	SND_SOC_DAPM_AIF_IN("DA_IN3", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN4", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),

	/* XXX SwapDA12_34 */

	SND_SOC_DAPM_MIXER("DA3 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA3, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA4 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA4, 0, NULL, 0),

	SND_SOC_DAPM_MUX("IHF Left Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_ihfl_select),
	SND_SOC_DAPM_MUX("IHF Right Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_ihfr_select),

	SND_SOC_DAPM_SWITCH("IHF Left", SND_SOC_NOPM, 0, 0, dapm_ihfl_mute),
	SND_SOC_DAPM_SWITCH("IHF Right", SND_SOC_NOPM, 0, 0, dapm_ihfr_mute),

	SND_SOC_DAPM_MIXER("IHFL DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHFL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("IHFR DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHFR, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("IHFL Enable", REG_ANACONF4,
			REG_ANACONF4_ENHFL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("IHFR Enable", REG_ANACONF4,
			REG_ANACONF4_ENHFR, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("IHFL"),
	SND_SOC_DAPM_OUTPUT("IHFR"),


	/* Vibrator path */

	SND_SOC_DAPM_AIF_IN("DA_IN5", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN6", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MIXER("DA5 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA5, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA6 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA6, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("VIB1 DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACVIB1, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("VIB2 DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACVIB2, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("PWMGEN1"),
	SND_SOC_DAPM_INPUT("PWMGEN2"),

	SND_SOC_DAPM_MUX("Vibra 1 Controller Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_pwm2vib1),
	SND_SOC_DAPM_MUX("Vibra 2 Controller Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_pwm2vib2),

	SND_SOC_DAPM_MIXER("VIB1 Enable", REG_ANACONF4,
			REG_ANACONF4_ENVIB1, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("VIB2 Enable", REG_ANACONF4,
			REG_ANACONF4_ENVIB2, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("VIB1"),
	SND_SOC_DAPM_OUTPUT("VIB2"),


	/* LineIn & Microphone 2 path */

	SND_SOC_DAPM_INPUT("LINL"),
	SND_SOC_DAPM_INPUT("LINR"),
	SND_SOC_DAPM_INPUT("MIC2"),

	SND_SOC_DAPM_SWITCH("LineIn Left", SND_SOC_NOPM, 0, 0, dapm_linl_mute),
	SND_SOC_DAPM_SWITCH("LineIn Right", SND_SOC_NOPM, 0, 0, dapm_linr_mute),
	SND_SOC_DAPM_SWITCH("Mic 2", SND_SOC_NOPM, 0, 0, dapm_mic2_mute),

	SND_SOC_DAPM_MIXER("LINL Enable", REG_ANACONF2,
			REG_ANACONF2_ENLINL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LINR Enable", REG_ANACONF2,
			REG_ANACONF2_ENLINR, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("MIC2 Enable", REG_ANACONF2,
			REG_ANACONF2_ENMIC2, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Mic 2 or LINR Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_mic2lr_select),

	SND_SOC_DAPM_MIXER("LINL ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCLINL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LINR ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCLINR, 0, NULL, 0),

	SND_SOC_DAPM_MUX("AD 1 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad1_select),
	SND_SOC_DAPM_MUX("AD 2 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad2_select),

	SND_SOC_DAPM_MIXER("AD1 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD2 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD1 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD12, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD2 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD12, 0, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("AD_OUT1", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT2", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),


	/* Microphone 1 path */

	SND_SOC_DAPM_INPUT("MIC1A"),
	SND_SOC_DAPM_INPUT("MIC1B"),

	SND_SOC_DAPM_MUX("Mic 1A or 1B Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_mic1ab_select),

	SND_SOC_DAPM_SWITCH("Mic 1", SND_SOC_NOPM, 0, 0, dapm_mic1_mute),

	SND_SOC_DAPM_MIXER("MIC1 Enable", REG_ANACONF2,
			REG_ANACONF2_ENMIC1, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("MIC1 ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCMIC, 0, NULL, 0),

	SND_SOC_DAPM_MUX("AD 3 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad3_select),

	SND_SOC_DAPM_MIXER("AD3 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD3 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD34, 0, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("AD_OUT3", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),


	/* HD Capture path */

	SND_SOC_DAPM_MUX("AD 5 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad5_select),
	SND_SOC_DAPM_MUX("AD 6 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad6_select),

	SND_SOC_DAPM_MIXER("AD5 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD6 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD57 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD5768, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD68 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD5768, 0, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("AD_OUT57", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT68", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),


	/* Digital Microphone path */

	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("DMIC3"),
	SND_SOC_DAPM_INPUT("DMIC4"),
	SND_SOC_DAPM_INPUT("DMIC5"),
	SND_SOC_DAPM_INPUT("DMIC6"),

	SND_SOC_DAPM_SWITCH("DMic 1", SND_SOC_NOPM, 0, 0, dapm_dmic1_mute),
	SND_SOC_DAPM_SWITCH("DMic 2", SND_SOC_NOPM, 0, 0, dapm_dmic2_mute),
	SND_SOC_DAPM_SWITCH("DMic 3", SND_SOC_NOPM, 0, 0, dapm_dmic3_mute),
	SND_SOC_DAPM_SWITCH("DMic 4", SND_SOC_NOPM, 0, 0, dapm_dmic4_mute),
	SND_SOC_DAPM_SWITCH("DMic 5", SND_SOC_NOPM, 0, 0, dapm_dmic5_mute),
	SND_SOC_DAPM_SWITCH("DMic 6", SND_SOC_NOPM, 0, 0, dapm_dmic6_mute),

	SND_SOC_DAPM_MIXER("AD4 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD4 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD34, 0, NULL, 0),

	SND_SOC_DAPM_AIF_OUT("AD_OUT4", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),


	/* LineIn Bypass path */

	SND_SOC_DAPM_MIXER("LINL to HSL Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LINR to HSR Gain", SND_SOC_NOPM, 0, 0, NULL, 0),


	/* Analog Loopback path */

	SND_SOC_DAPM_MIXER("AD1 to IHFL Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD2 to IHFR Gain", SND_SOC_NOPM, 0, 0, NULL, 0),


	/* Acoustical Noise Cancellation path */

	SND_SOC_DAPM_MUX("ANC Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_anc_in_select),

	SND_SOC_DAPM_MUX("ANC Playback Switch",
			SND_SOC_NOPM, 0, 0, dapm_anc_enable),

	SND_SOC_DAPM_SWITCH("ANC to Earpiece",
			SND_SOC_NOPM, 0, 0, dapm_anc_ear_mute),


	/* Sidetone Filter path */

	SND_SOC_DAPM_MUX("Sidetone Left Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_stfir1_in_select),
	SND_SOC_DAPM_MUX("Sidetone Right Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_stfir2_in_select),

	SND_SOC_DAPM_MIXER("STFIR1 Control", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("STFIR2 Control", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("STFIR1 Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("STFIR2 Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route intercon[] = {
	/* Headset path */

	{"DA1 Channel Gain", NULL, "DA_IN1"},
	{"DA2 Channel Gain", NULL, "DA_IN2"},

	{"HSL Digital Gain", NULL, "DA1 Channel Gain"},
	{"HSR Digital Gain", NULL, "DA2 Channel Gain"},

	{"HSL DAC", NULL, "HSL Digital Gain"},
	{"HSR DAC", NULL, "HSR Digital Gain"},

	{"HSL DAC Driver", NULL, "HSL DAC"},
	{"HSR DAC Driver", NULL, "HSR DAC"},

	{"Headset Left", "Playback Switch", "HSL DAC Driver"},
	{"Headset Right", "Playback Switch", "HSR DAC Driver"},

	{"HSL Enable", NULL, "Headset Left"},
	{"HSR Enable", NULL, "Headset Right"},

	{"Charge Pump", NULL, "HSL Enable"},
	{"Charge Pump", NULL, "HSR Enable"},

	{"HSL", NULL, "Charge Pump"},
	{"HSR", NULL, "Charge Pump"},


	/* Earpiece path */

	{"Earpiece Source Playback Route", "Headset Left", "HSL Digital Gain"},
	{"Earpiece Source Playback Route", "IHF Left", "IHF Left"},

	{"EAR DAC", NULL, "Earpiece Source Playback Route"},

	{"Earpiece", "Playback Switch", "EAR DAC"},

	{"EAR Enable", NULL, "Earpiece"},

	{"EAR", NULL, "EAR Enable"},


	/* Handsfree path */

	{"DA3 Channel Gain", NULL, "DA_IN3"},
	{"DA4 Channel Gain", NULL, "DA_IN4"},

	{"IHF Left Source Playback Route", "Audio Path", "DA3 Channel Gain"},
	{"IHF Right Source Playback Route", "Audio Path", "DA4 Channel Gain"},

	{"IHF Left", "Playback Switch", "IHF Left Source Playback Route"},
	{"IHF Right", "Playback Switch", "IHF Right Source Playback Route"},

	{"IHFL DAC", NULL, "IHF Left"},
	{"IHFR DAC", NULL, "IHF Right"},

	{"IHFL Enable", NULL, "IHFL DAC"},
	{"IHFR Enable", NULL, "IHFR DAC"},

	{"IHFL", NULL, "IHFL Enable"},
	{"IHFR", NULL, "IHFR Enable"},


	/* Vibrator path */

	{"DA5 Channel Gain", NULL, "DA_IN5"},
	{"DA6 Channel Gain", NULL, "DA_IN6"},

	{"VIB1 DAC", NULL, "DA5 Channel Gain"},
	{"VIB2 DAC", NULL, "DA6 Channel Gain"},

	{"Vibra 1 Controller Playback Route", "Audio Path", "VIB1 DAC"},
	{"Vibra 2 Controller Playback Route", "Audio Path", "VIB2 DAC"},
	{"Vibra 1 Controller Playback Route", "PWM Generator", "PWMGEN1"},
	{"Vibra 2 Controller Playback Route", "PWM Generator", "PWMGEN2"},

	{"VIB1 Enable", NULL, "Vibra 1 Controller Playback Route"},
	{"VIB2 Enable", NULL, "Vibra 2 Controller Playback Route"},

	{"VIB1", NULL, "VIB1 Enable"},
	{"VIB2", NULL, "VIB2 Enable"},


	/* LineIn & Microphone 2 path */

	{"LineIn Left", "Capture Switch", "LINL"},
	{"LineIn Right", "Capture Switch", "LINR"},
	{"Mic 2", "Capture Switch", "MIC2"},

	{"LINL Enable", NULL, "LineIn Left"},
	{"LINR Enable", NULL, "LineIn Right"},
	{"MIC2 Enable", NULL, "Mic 2"},

	{"Mic 2 or LINR Select Capture Route", "LineIn Right", "LINR Enable"},
	{"Mic 2 or LINR Select Capture Route", "Mic 2", "MIC2 Enable"},

	{"LINL ADC", NULL, "LINL Enable"},
	{"LINR ADC", NULL, "Mic 2 or LINR Select Capture Route"},

	{"AD 1 Select Capture Route", "LineIn Left", "LINL ADC"},
	{"AD 2 Select Capture Route", "LineIn Right", "LINR ADC"},

	{"AD1 Channel Gain", NULL, "AD 1 Select Capture Route"},
	{"AD2 Channel Gain", NULL, "AD 2 Select Capture Route"},

	{"AD1 Enable", NULL, "AD1 Channel Gain"},
	{"AD2 Enable", NULL, "AD2 Channel Gain"},

	{"AD_OUT1", NULL, "AD1 Enable"},
	{"AD_OUT2", NULL, "AD2 Enable"},


	/* Microphone 1 path */

	{"Mic 1A or 1B Select Capture Route", "Mic 1A", "MIC1A"},
	{"Mic 1A or 1B Select Capture Route", "Mic 1B", "MIC1B"},

	{"Mic 1", "Capture Switch", "Mic 1A or 1B Select Capture Route"},

	{"MIC1 Enable", NULL, "Mic 1"},

	{"MIC1 ADC", NULL, "MIC1 Enable"},

	{"AD 3 Select Capture Route", "Mic 1", "MIC1 ADC"},

	{"AD3 Channel Gain", NULL, "AD 3 Select Capture Route"},

	{"AD3 Enable", NULL, "AD3 Channel Gain"},

	{"AD_OUT3", NULL, "AD3 Enable"},


	/* HD Capture path */

	{"AD 5 Select Capture Route", "Mic 2", "LINR ADC"},
	{"AD 6 Select Capture Route", "Mic 1", "MIC1 ADC"},

	{"AD5 Channel Gain", NULL, "AD 5 Select Capture Route"},
	{"AD6 Channel Gain", NULL, "AD 6 Select Capture Route"},

	{"AD57 Enable", NULL, "AD5 Channel Gain"},
	{"AD68 Enable", NULL, "AD6 Channel Gain"},

	{"AD_OUT57", NULL, "AD57 Enable"},
	{"AD_OUT68", NULL, "AD68 Enable"},


	/* Digital Microphone path */

	{"DMic 1", "Capture Switch", "DMIC1"},
	{"DMic 2", "Capture Switch", "DMIC2"},
	{"DMic 3", "Capture Switch", "DMIC3"},
	{"DMic 4", "Capture Switch", "DMIC4"},
	{"DMic 5", "Capture Switch", "DMIC5"},
	{"DMic 6", "Capture Switch", "DMIC6"},

	{"AD 1 Select Capture Route", "DMic 1", "DMic 1"},
	{"AD 2 Select Capture Route", "DMic 2", "DMic 2"},
	{"AD 3 Select Capture Route", "DMic 3", "DMic 3"},
	{"AD 5 Select Capture Route", "DMic 5", "DMic 5"},
	{"AD 6 Select Capture Route", "DMic 6", "DMic 6"},

	{"AD4 Channel Gain", NULL, "DMic 4"},

	{"AD4 Enable", NULL, "AD4 Channel Gain"},

	{"AD_OUT4", NULL, "AD4 Enable"},


	/* LineIn Bypass path */

	{"LINL to HSL Gain", NULL, "LINL Enable"},
	{"LINR to HSR Gain", NULL, "LINR Enable"},

	{"HSL DAC Driver", NULL, "LINL to HSL Gain"},
	{"HSR DAC Driver", NULL, "LINR to HSR Gain"},


	/* Analog Loopback path */

	{"AD1 to IHFL Gain", NULL, "AD1 Channel Gain"},
	{"AD2 to IHFR Gain", NULL, "AD2 Channel Gain"},

	{"IHFL DAC", NULL, "AD1 to IHFL Gain"},
	{"IHFR DAC", NULL, "AD2 to IHFR Gain"},


	/* Acoustical Noise Cancellation path */

	{"ANC Source Playback Route", "Mic 2 / DMic 5", "AD5 Channel Gain"},
	{"ANC Source Playback Route", "Mic 1 / DMic 6", "AD6 Channel Gain"},

	{"ANC Playback Switch", "Enabled", "ANC Source Playback Route"},

	{"IHF Left Source Playback Route", "ANC", "ANC Playback Switch"},
	{"IHF Right Source Playback Route", "ANC", "ANC Playback Switch"},
	{"ANC to Earpiece", "Playback Switch", "ANC Playback Switch"},

	{"HSL Digital Gain", NULL, "ANC to Earpiece"},


	/* Sidetone Filter path */

	{"Sidetone Left Source Playback Route", "LineIn Left", "AD1 Enable"},
	{"Sidetone Left Source Playback Route", "LineIn Right", "AD2 Enable"},
	{"Sidetone Left Source Playback Route", "Mic 1", "AD3 Enable"},
	{"Sidetone Left Source Playback Route", "Headset Left", "DA_IN1"},
	{"Sidetone Right Source Playback Route", "LineIn Right", "AD2 Enable"},
	{"Sidetone Right Source Playback Route", "Mic 1", "AD3 Enable"},
	{"Sidetone Right Source Playback Route", "DMic 4", "AD4 Enable"},
	{"Sidetone Right Source Playback Route", "Headset Right", "DA_IN2"},

	{"STFIR1 Control", NULL, "Sidetone Left Source Playback Route"},
	{"STFIR2 Control", NULL, "Sidetone Right Source Playback Route"},

	{"STFIR1 Gain", NULL, "STFIR1 Control"},
	{"STFIR2 Gain", NULL, "STFIR2 Control"},

	{"DA1 Channel Gain", NULL, "STFIR1 Gain"},
	{"DA2 Channel Gain", NULL, "STFIR2 Gain"},
};

/* from -31 to 31 dB in 1 dB steps (mute instead of -32 dB) */
static DECLARE_TLV_DB_SCALE(adx_dig_gain_tlv, -3200, 100, 1);

/* from -62 to 0 dB in 1 dB steps (mute instead of -63 dB) */
static DECLARE_TLV_DB_SCALE(dax_dig_gain_tlv, -6300, 100, 1);

/* from 0 to 8 dB in 1 dB steps (mute instead of -1 dB) */
static DECLARE_TLV_DB_SCALE(hs_ear_dig_gain_tlv, -100, 100, 1);

/* from -30 to 0 dB in 1 dB steps (mute instead of -31 dB) */
static DECLARE_TLV_DB_SCALE(stfir_dig_gain_tlv, -3100, 100, 1);

/* from -32 to -20 dB in 4 dB steps / from -18 to 2 dB in 2 dB steps */
static const unsigned int hs_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 3, TLV_DB_SCALE_ITEM(-3200, 400, 0),
	4, 15, TLV_DB_SCALE_ITEM(-1800, 200, 0),
};

/* from 0 to 31 dB in 1 dB steps */
static DECLARE_TLV_DB_SCALE(mic_gain_tlv, 0, 100, 0);

/* from -10 to 20 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(lin_gain_tlv, -1000, 200, 0);

/* from -36 to 0 dB in 2 dB steps (mute instead of -38 dB) */
static DECLARE_TLV_DB_SCALE(lin2hs_gain_tlv, -3800, 200, 1);

static const char *enum_ena_dis[] = {"Enabled", "Disabled"};
static const char *enum_dis_ena[] = {"Disabled", "Enabled"};
static SOC_ENUM_SINGLE_DECL(soc_enum_hshpen,
	REG_ANACONF1, REG_ANACONF1_HSHPEN, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_hslowpow,
	REG_ANACONF1, REG_ANACONF1_HSLOWPOW, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_daclowpow1,
	REG_ANACONF1, REG_ANACONF1_DACLOWPOW1, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_daclowpow0,
	REG_ANACONF1, REG_ANACONF1_DACLOWPOW0, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_eardaclowpow,
	REG_ANACONF1, REG_ANACONF1_EARDACLOWPOW, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_eardrvlowpow,
	REG_ANACONF1, REG_ANACONF1_EARDRVLOWPOW, enum_dis_ena);

static const char *enum_earselcm[] = {"0.95V", "1.10V", "1.27V", "1.58V"};
static SOC_ENUM_SINGLE_DECL(soc_enum_earselcm,
	REG_ANACONF1, REG_ANACONF1_EARSELCM, enum_earselcm);

static const char *enum_hsfadspeed[] = {"2ms", "0.5ms", "10.6ms", "5ms"};
static SOC_ENUM_SINGLE_DECL(soc_enum_hsfadspeed,
	REG_DIGMICCONF, REG_DIGMICCONF_HSFADSPEED, enum_hsfadspeed);

static const char *enum_ensemicx[] = {"Differential", "Single Ended"};
static SOC_ENUM_SINGLE_DECL(soc_enum_ensemic1,
	REG_ANAGAIN1, REG_ANAGAINX_ENSEMICX, enum_ensemicx);
static SOC_ENUM_SINGLE_DECL(soc_enum_ensemic2,
	REG_ANAGAIN2, REG_ANAGAINX_ENSEMICX, enum_ensemicx);
static SOC_ENUM_SINGLE_DECL(soc_enum_lowpowmic1,
	REG_ANAGAIN1, REG_ANAGAINX_LOWPOWMICX, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_lowpowmic2,
	REG_ANAGAIN2, REG_ANAGAINX_LOWPOWMICX, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_ad12nh, REG_ADFILTCONF,
	REG_ADFILTCONF_AD1NH, REG_ADFILTCONF_AD2NH, enum_ena_dis);
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad34nh, REG_ADFILTCONF,
	REG_ADFILTCONF_AD3NH, REG_ADFILTCONF_AD4NH, enum_ena_dis);

static const char *enum_av_mode[] = {"Audio", "Voice"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad12voice, REG_ADFILTCONF,
	REG_ADFILTCONF_AD1VOICE, REG_ADFILTCONF_AD2VOICE, enum_av_mode);
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad34voice, REG_ADFILTCONF,
	REG_ADFILTCONF_AD3VOICE, REG_ADFILTCONF_AD4VOICE, enum_av_mode);

static SOC_ENUM_SINGLE_DECL(soc_enum_da12voice,
	REG_DASLOTCONF1, REG_DASLOTCONF1_DA12VOICE, enum_av_mode);
static SOC_ENUM_SINGLE_DECL(soc_enum_da34voice,
	REG_DASLOTCONF3, REG_DASLOTCONF3_DA34VOICE, enum_av_mode);
static SOC_ENUM_SINGLE_DECL(soc_enum_da56voice,
	REG_DASLOTCONF5, REG_DASLOTCONF5_DA56VOICE, enum_av_mode);

static SOC_ENUM_SINGLE_DECL(soc_enum_swapda12_34,
	REG_DASLOTCONF1, REG_DASLOTCONF1_SWAPDA12_34, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_vib12swap, REG_CLASSDCONF1,
	REG_CLASSDCONF1_VIB1SWAPEN, REG_CLASSDCONF1_VIB2SWAPEN, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_hflrswap, REG_CLASSDCONF1,
	REG_CLASSDCONF1_HFLSWAPEN, REG_CLASSDCONF1_HFRSWAPEN, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_fir01byp, REG_CLASSDCONF2,
	REG_CLASSDCONF2_FIRBYP0, REG_CLASSDCONF2_FIRBYP1, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_fir23byp, REG_CLASSDCONF2,
	REG_CLASSDCONF2_FIRBYP2, REG_CLASSDCONF2_FIRBYP3, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_highvol01, REG_CLASSDCONF2,
	REG_CLASSDCONF2_HIGHVOLEN0, REG_CLASSDCONF2_HIGHVOLEN1, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_highvol23, REG_CLASSDCONF2,
	REG_CLASSDCONF2_HIGHVOLEN2, REG_CLASSDCONF2_HIGHVOLEN3, enum_dis_ena);

static const char *enum_sinc53[] = {"Sinc 5", "Sinc 3"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic12sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC1SINC3, REG_DMICFILTCONF_DMIC2SINC3, enum_sinc53);
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic34sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC3SINC3, REG_DMICFILTCONF_DMIC4SINC3, enum_sinc53);
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic56sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC5SINC3, REG_DMICFILTCONF_DMIC6SINC3, enum_sinc53);

static const char *enum_da2hslr[] = {"Sidetone", "Audio Path"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_da2hslr, REG_DIGMULTCONF1,
	REG_DIGMULTCONF1_DATOHSLEN, REG_DIGMULTCONF1_DATOHSREN,	enum_da2hslr);

static const char *enum_sinc31[] = {"Sinc 3", "Sinc 1"};
static SOC_ENUM_SINGLE_DECL(soc_enum_hsesinc,
		REG_HSLEARDIGGAIN, REG_HSLEARDIGGAIN_HSSINC1, enum_sinc31);

static const char *enum_fadespeed[] = {"1ms", "4ms", "8ms", "16ms"};
static SOC_ENUM_SINGLE_DECL(soc_enum_fadespeed,
	REG_HSRDIGGAIN, REG_HSRDIGGAIN_FADESPEED, enum_fadespeed);

/* XXX move to DAPM */
static SOC_ENUM_SINGLE_DECL(soc_enum_enfirsids,
	REG_SIDFIRCONF, REG_SIDFIRCONF_ENFIRSIDS, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_parlhf,
	REG_CLASSDCONF1, REG_CLASSDCONF1_PARLHF, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_parlvib,
	REG_CLASSDCONF1, REG_CLASSDCONF1_PARLVIB, enum_dis_ena);

static struct snd_kcontrol_new ab8500_snd_controls[] = {
	SOC_ENUM("Headset High Pass Playback Switch", soc_enum_hshpen),
	SOC_ENUM("Headset Low Power Playback Switch", soc_enum_hslowpow),
	SOC_ENUM("Headset DAC Low Power Playback Switch", soc_enum_daclowpow1),
	SOC_ENUM("Headset DAC Drv Low Power Playback Switch",
		soc_enum_daclowpow0),
	SOC_ENUM("Earpiece DAC Low Power Playback Switch",
		soc_enum_eardaclowpow),
	SOC_ENUM("Earpiece DAC Drv Low Power Playback Switch",
		soc_enum_eardrvlowpow),
	SOC_ENUM("Earpiece Common Mode Playback Switch", soc_enum_earselcm),

	SOC_ENUM("Headset Fade Speed Playback Switch", soc_enum_hsfadspeed),

	SOC_ENUM("Mic 1 Type Capture Switch", soc_enum_ensemic1),
	SOC_ENUM("Mic 2 Type Capture Switch", soc_enum_ensemic2),
	SOC_ENUM("Mic 1 Low Power Capture Switch", soc_enum_lowpowmic1),
	SOC_ENUM("Mic 2 Low Power Capture Switch", soc_enum_lowpowmic2),

	SOC_ENUM("LineIn High Pass Capture Switch", soc_enum_ad12nh),
	SOC_ENUM("Mic High Pass Capture Switch", soc_enum_ad34nh),
	SOC_ENUM("LineIn Mode Capture Switch", soc_enum_ad12voice),
	SOC_ENUM("Mic Mode Capture Switch", soc_enum_ad34voice),

	SOC_ENUM("Headset Mode Playback Switch", soc_enum_da12voice),
	SOC_ENUM("IHF Mode Playback Switch", soc_enum_da34voice),
	SOC_ENUM("Vibra Mode Playback Switch", soc_enum_da56voice),

	SOC_ENUM("IHF and Headset Swap Playback Switch", soc_enum_swapda12_34),

	SOC_ENUM("IHF Low EMI Mode Playback Switch", soc_enum_hflrswap),
	SOC_ENUM("Vibra Low EMI Mode Playback Switch", soc_enum_vib12swap),

	SOC_ENUM("IHF FIR Bypass Playback Switch", soc_enum_fir01byp),
	SOC_ENUM("Vibra FIR Bypass Playback Switch", soc_enum_fir23byp),

	/* XXX Cannot be changed on the fly with digital channel enabled. */
	SOC_ENUM("IHF High Volume Playback Switch", soc_enum_highvol01),
	SOC_ENUM("Vibra High Volume Playback Switch", soc_enum_highvol23),

	SOC_SINGLE("ClassD High Pass Gain Playback Volume",
		REG_CLASSDCONF3, REG_CLASSDCONF3_DITHHPGAIN,
		REG_CLASSDCONF3_DITHHPGAIN_MAX, NORMAL),
	SOC_SINGLE("ClassD White Gain Playback Volume",
		REG_CLASSDCONF3, REG_CLASSDCONF3_DITHWGAIN,
		REG_CLASSDCONF3_DITHWGAIN_MAX, NORMAL),

	SOC_ENUM("LineIn Filter Capture Switch", soc_enum_dmic12sinc),
	SOC_ENUM("Mic Filter Capture Switch", soc_enum_dmic34sinc),
	SOC_ENUM("HD Mic Filter Capture Switch", soc_enum_dmic56sinc),

	SOC_ENUM("Headset Source Playback Route", soc_enum_da2hslr),

	/* XXX Cannot be changed on the fly with digital channel enabled. */
	SOC_ENUM("Headset Filter Playback Switch", soc_enum_hsesinc),

	SOC_ENUM("Digital Gain Fade Speed Switch", soc_enum_fadespeed),

	SOC_DOUBLE_R("Vibra PWM Duty Cycle N Playback Volume",
		REG_PWMGENCONF3, REG_PWMGENCONF5,
		REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, NORMAL),
	SOC_DOUBLE_R("Vibra PWM Duty Cycle P Playback Volume",
		REG_PWMGENCONF2, REG_PWMGENCONF4,
		REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, NORMAL),

	/* XXX move to DAPM */
	SOC_ENUM("Sidetone Playback Switch", soc_enum_enfirsids),
	SOC_ENUM("IHF L and R Bridge Playback Route", soc_enum_parlhf),
	SOC_ENUM("Vibra 1 and 2 Bridge Playback Route", soc_enum_parlvib),

	/* Digital gains for AD side */

	SOC_DOUBLE_R_TLV("LineIn Master Gain Capture Volume",
		REG_ADDIGGAIN1, REG_ADDIGGAIN2,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Mic Master Gain Capture Volume",
		REG_ADDIGGAIN3, REG_ADDIGGAIN4,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("HD Mic Master Gain Capture Volume",
		REG_ADDIGGAIN5, REG_ADDIGGAIN6,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),

	/* Digital gains for DA side */

	SOC_DOUBLE_R_TLV("Headset Master Gain Playback Volume",
		REG_DADIGGAIN1, REG_DADIGGAIN2,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("IHF Master Gain Playback Volume",
		REG_DADIGGAIN3, REG_DADIGGAIN4,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Vibra Master Gain Playback Volume",
		REG_DADIGGAIN5, REG_DADIGGAIN6,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Analog Loopback Gain Playback Volume",
		REG_ADDIGLOOPGAIN1, REG_ADDIGLOOPGAIN2,
		0, REG_ADDIGLOOPGAINX_ADXLBGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Headset Digital Gain Playback Volume",
		REG_HSLEARDIGGAIN, REG_HSRDIGGAIN,
		0, REG_HSLEARDIGGAIN_HSLDGAIN_MAX, INVERT, hs_ear_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Sidetone Digital Gain Playback Volume",
		REG_SIDFIRGAIN1, REG_SIDFIRGAIN2,
		0, REG_SIDFIRGAINX_FIRSIDXGAIN_MAX, INVERT, stfir_dig_gain_tlv),

	/* Analog gains */

	SOC_DOUBLE_TLV("Headset Gain Playback Volume",
		REG_ANAGAIN3,
		REG_ANAGAIN3_HSLGAIN, REG_ANAGAIN3_HSRGAIN,
		REG_ANAGAIN3_HSXGAIN_MAX, INVERT, hs_gain_tlv),
	SOC_SINGLE_TLV("Mic 1 Capture Volume",
		REG_ANAGAIN1,
		REG_ANAGAINX_MICXGAIN,
		REG_ANAGAINX_MICXGAIN_MAX, NORMAL, mic_gain_tlv),
	SOC_SINGLE_TLV("Mic 2 Capture Volume",
		REG_ANAGAIN2,
		REG_ANAGAINX_MICXGAIN,
		REG_ANAGAINX_MICXGAIN_MAX, NORMAL, mic_gain_tlv),
	SOC_DOUBLE_TLV("LineIn Capture Volume",
		REG_ANAGAIN4,
		REG_ANAGAIN4_LINLGAIN, REG_ANAGAIN4_LINRGAIN,
		REG_ANAGAIN4_LINXGAIN_MAX, NORMAL, lin_gain_tlv),
	SOC_DOUBLE_R_TLV("LineIn to Headset Bypass Playback Volume",
		REG_DIGLINHSLGAIN, REG_DIGLINHSRGAIN,
		REG_DIGLINHSXGAIN_LINTOHSXGAIN,
		REG_DIGLINHSXGAIN_LINTOHSXGAIN_MAX, INVERT, lin2hs_gain_tlv),
};

static int ab8500_add_widgets(struct snd_soc_codec *codec)
{
	int ret;

	ret = snd_soc_dapm_new_controls(&codec->dapm, ab8500_dapm_widgets,
			ARRAY_SIZE(ab8500_dapm_widgets));
	if (ret < 0) {
		pr_err("%s: Failed to create DAPM controls (%d).\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&codec->dapm, intercon,
			ARRAY_SIZE(intercon));
	if (ret < 0) {
		pr_err("%s: Failed to add DAPM routes (%d).\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static int ab8500_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);
	return 0;
}

static int ab8500_pcm_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);
	return 0;
}

static int ab8500_pcm_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);

	/* Clear interrupt status registers by reading them. */
	ab8500_audio_read_reg(dai->codec, REG_AUDINTSOURCE1);
	ab8500_audio_read_reg(dai->codec, REG_AUDINTSOURCE2);

	return 0;
}

static void ab8500_pcm_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);
	ab8500_audio_dump_all_reg(dai->codec);
}

static int ab8500_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	pr_debug("%s Enter.\n", __func__);
	return 0;
}

/* Sets Master/Slave relations according format mask */
static int set_dai_relationship(struct snd_soc_codec *codec, unsigned int fmt)
{
	unsigned int clear_mask;
	unsigned int set_mask;

	clear_mask = BMASK(REG_DIGIFCONF3_IF1DATOIF0AD) |
			BMASK(REG_DIGIFCONF3_IF1CLKTOIF0CLK) |
			BMASK(REG_DIGIFCONF3_IF0BFIFOEN) |
			BMASK(REG_DIGIFCONF3_IF0MASTER);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* codec clk & FRM master */
		pr_info("- Codec is a master\n");
		set_mask |= BMASK(REG_DIGIFCONF3_IF0MASTER);
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* codec clk & FRM slave */
		pr_info("- Codec is a slave\n");
		break;
	case SND_SOC_DAIFMT_CBS_CFM: /* codec clk slave & FRM master */
	case SND_SOC_DAIFMT_CBM_CFS: /* codec clk master & frame slave */
		pr_err("The device is either a master or a slave.\n");
	default:
		pr_err("Unsupporter master mask 0x%x\n",
				(fmt & SND_SOC_DAIFMT_MASTER_MASK));
		return -EINVAL;
		break;
	}

	ab8500_update_audio_reg(codec, REG_DIGIFCONF3, clear_mask, set_mask);

	return 0;
}

/* Gates clocking according format mask */
static int set_dai_clock_gate(struct snd_soc_codec *codec, unsigned int fmt)
{
	unsigned int clear_mask;
	unsigned int set_mask;

	clear_mask = BMASK(REG_DIGIFCONF1_ENMASTGEN) |
			BMASK(REG_DIGIFCONF1_ENFSBITCLK0);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT: /* continuous clock */
		pr_info("- Clock is not gated\n");
		set_mask |= BMASK(REG_DIGIFCONF1_ENMASTGEN);
		set_mask |= BMASK(REG_DIGIFCONF1_ENFSBITCLK0);
		break;
	case SND_SOC_DAIFMT_GATED: /* clock is gated */
		pr_info("- Clock IS gated\n");
		break;
	default:
		pr_err("Unsupporter clock mask 0x%x\n",
				(fmt & SND_SOC_DAIFMT_CLOCK_MASK));
		return -EINVAL;
		break;
	}

	ab8500_update_audio_reg(codec, REG_DIGIFCONF1, clear_mask, set_mask);

	return 0;
}

static int ab8500_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	unsigned int clear_mask;
	unsigned int set_mask;
	struct snd_soc_codec *codec = dai->codec;
	int err;

	pr_debug("%s: fmt = 0x%x\n", __func__, fmt);

	/* Set Master/Slave */
	err = set_dai_relationship(codec, fmt);
	if (err) {
		pr_err("%s: Failed to set master/slave (%d).\n", __func__, err);
		return err;
	}

	/* Set clock enable/disable */
	err = set_dai_clock_gate(codec, fmt);
	if (err) {
		pr_err("%s: Failed to set clock gate (%d).\n", __func__, err);
		return err;
	}

	/* Setting data transfer format */
	clear_mask = REG_MASK_ALL;
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S: /* I2S mode */
		pr_info("- FORMAT I2S\n");
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT1);
		set_mask |= BMASK(REG_DIGIFCONF2_IF0DEL);

		/* 32 bit */
		set_mask |= BMASK(REG_DIGIFCONF2_IF0WL1) |
			BMASK(REG_DIGIFCONF2_IF0WL0);
		break;
	case SND_SOC_DAIFMT_DSP_A: /* L data MSB after FRM LRC */
		pr_info("- FORMAT DSP A\n");
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT0);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* L data MSB during FRM LRC */
		pr_info("- FORMAT DSP B\n");
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT0);
		break;
	default:
		pr_err("Unsupporter format 0x%x\n",
				(fmt & SND_SOC_DAIFMT_FORMAT_MASK));
		return -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* normal bit clock + frame */
		pr_info("- Normal bit clock, normal frame\n");
		break;
	case SND_SOC_DAIFMT_NB_IF: /* normal BCLK + inv FRM */
		pr_info("- Normal bit clock, inverted frame\n");
		set_mask |= BMASK(REG_DIGIFCONF2_FSYNC0P);
		break;
	case SND_SOC_DAIFMT_IB_NF: /* invert BCLK + nor FRM */
		pr_info("- inverted bit clock, normal frame\n");
		set_mask |= BMASK(REG_DIGIFCONF2_BITCLK0P);
		break;
	case SND_SOC_DAIFMT_IB_IF: /* invert BCLK + FRM */
		pr_info("- inverted bit clock, inverted frame\n");
		set_mask |= BMASK(REG_DIGIFCONF2_FSYNC0P);
		set_mask |= BMASK(REG_DIGIFCONF2_BITCLK0P);
		break;
	default:
		pr_err("Unsupported INV mask 0x%x\n",
				(fmt & SND_SOC_DAIFMT_INV_MASK));
		return -EINVAL;
		break;
	}

	ab8500_update_audio_reg(codec, REG_DIGIFCONF2, clear_mask, set_mask);

	return 0;
}

static int ab8500_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	int data;
	struct snd_soc_codec *codec = dai->codec;
	unsigned int clear_mask;

	/* Only 16 bit slot width is supported at the moment in TDM mode */
	if (slot_width != 16) {
		pr_err("%s: Unsupported slot_width %d.\n",
			__func__, slot_width);
		return -EINVAL;
	}

	/* Set the TDM clocking according to slot count */
	switch (slots) {
	case 2:
		data = REG_MASK_NONE;
		break;
	case 4:
		data = BMASK(REG_DIGIFCONF1_IF0BITCLKOS0);
		break;
	case 8:
		data = BMASK(REG_DIGIFCONF1_IF0BITCLKOS1);
		break;
	case 16:
		data = BMASK(REG_DIGIFCONF1_IF0BITCLKOS0) |
				BMASK(REG_DIGIFCONF1_IF0BITCLKOS1);
		break;
	default:
		pr_err("%s: Unsupported slots %d.\n", __func__, slots);
		return -EINVAL;
	}

	ab8500_update_audio_reg(codec, REG_DIGIFCONF1,
		BMASK(REG_DIGIFCONF1_IF0BITCLKOS0) |
		BMASK(REG_DIGIFCONF1_IF0BITCLKOS1),
		data);

	/* XXX Make slot configuration as a control */

	clear_mask = REG_DASLOTCONFX_SLTODAX_MASK;

	/* DA_IN1/3/5 receives slot 9, DA_IN2/4/6 receives slot 11 */
	ab8500_update_audio_reg(codec, REG_DASLOTCONF1, clear_mask, 9);
	ab8500_update_audio_reg(codec, REG_DASLOTCONF2, clear_mask, 11);
	ab8500_update_audio_reg(codec, REG_DASLOTCONF3, clear_mask, 9);
	ab8500_update_audio_reg(codec, REG_DASLOTCONF4, clear_mask, 11);
	ab8500_update_audio_reg(codec, REG_DASLOTCONF5, clear_mask, 9);
	ab8500_update_audio_reg(codec, REG_DASLOTCONF6, clear_mask, 11);

	/* AD_OUT3 transmits slots 0 & 1 */
	ab8500_update_audio_reg(codec, REG_ADSLOTSEL1,
			REG_MASK_ALL,
			BMASK(REG_ADSLOTSELX_ODDX_1) |
			BMASK(REG_ADSLOTSELX_EVENX_1));

	return 0;
}

struct snd_soc_dai_driver ab8500_codec_dai[] = {
	{
		.name = "ab8500-codec-dai.0",
		.id = 0,
		.playback = {
			.stream_name = "ab8500_0p",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AB8500_SUPPORTED_RATE,
			.formats = AB8500_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab8500_pcm_startup,
				.prepare = ab8500_pcm_prepare,
				.hw_params = ab8500_pcm_hw_params,
				.shutdown = ab8500_pcm_shutdown,
				.set_sysclk = ab8500_set_dai_sysclk,
				.set_tdm_slot = ab8500_set_dai_tdm_slot,
				.set_fmt = ab8500_set_dai_fmt,
			}
		},
		.symmetric_rates = 1
	},
	{
		.name = "ab8500-codec-dai.1",
		.id = 1,
		.capture = {
			.stream_name = "ab8500_0c",
			.channels_min = 1,
			.channels_max = 1,
			.rates = AB8500_SUPPORTED_RATE,
			.formats = AB8500_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab8500_pcm_startup,
				.prepare = ab8500_pcm_prepare,
				.hw_params = ab8500_pcm_hw_params,
				.shutdown = ab8500_pcm_shutdown,
				.set_sysclk = ab8500_set_dai_sysclk,
				.set_tdm_slot = ab8500_set_dai_tdm_slot,
				.set_fmt = ab8500_set_dai_fmt,
			}
		},
		.symmetric_rates = 1
	}
};

/* Configures audio macrocell into the AB8500 Chip */
static void configure_audio_macrocell(struct snd_soc_codec *codec)
{
	int data;

	data = ab8500_read_reg(codec, AB8500_SYS_CTRL2_BLOCK, AB8500_CTRL3_REG);
	data &= ~CLK_32K_OUT2_DISABLE;
	ab8500_write_reg(codec, AB8500_SYS_CTRL2_BLOCK, AB8500_CTRL3_REG, data);
	data |= INACTIVE_RESET_AUDIO;
	ab8500_write_reg(codec, AB8500_SYS_CTRL2_BLOCK, AB8500_CTRL3_REG, data);

	data = ab8500_read_reg(codec, AB8500_SYS_CTRL2_BLOCK,
		AB8500_SYSULPCLK_CTRL1_REG);
	data |= ENABLE_AUDIO_CLK_TO_AUDIO_BLK;
	ab8500_write_reg(codec, AB8500_SYS_CTRL2_BLOCK,
		AB8500_SYSULPCLK_CTRL1_REG, data);

	data = ab8500_read_reg(codec, AB8500_MISC, AB8500_GPIO_DIR4_REG);
	data |= GPIO27_DIR_OUTPUT | GPIO29_DIR_OUTPUT | GPIO31_DIR_OUTPUT;
	ab8500_write_reg(codec, AB8500_MISC, AB8500_GPIO_DIR4_REG, data);

	data = ab8500_read_reg(codec, AB8500_MISC, AB8500_GPIO_DIR5_REG);
	data |= GPIO35_DIR_OUTPUT;
	ab8500_write_reg(codec, AB8500_MISC, AB8500_GPIO_DIR5_REG, data);
	data = ab8500_read_reg(codec, AB8500_MISC, AB8500_GPIO_OUT5_REG);
	data |= GPIO35_DIR_OUTPUT;
	ab8500_write_reg(codec, AB8500_MISC, AB8500_GPIO_OUT5_REG, data);
}

static int ab8500_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY)
			enable_regulator("v-audio");
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(msecs_to_jiffies(100));
			disable_regulator("v-audio");
		}
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static int ab8500_codec_probe(struct snd_soc_codec *codec)
{
	int i, ret;
	u8 *cache = codec->reg_cache;

	pr_debug("%s: Enter.\n", __func__);

	configure_audio_macrocell(codec);

	for (i = REG_AUDREV; i >= REG_POWERUP; i--)
		ab8500_audio_write_reg(codec, i, cache[i]);

	ret = snd_soc_add_controls(codec, ab8500_snd_controls,
			ARRAY_SIZE(ab8500_snd_controls));
	if (ret < 0) {
		pr_err("%s: failed to add soc controls (%d).\n",
				__func__, ret);
		return ret;
	}

	ret = ab8500_add_widgets(codec);
	if (ret < 0) {
		pr_err("%s: Failed add widgets (%d).\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int ab8500_codec_remove(struct snd_soc_codec *codec)
{
	snd_soc_dapm_free(&codec->dapm);

	return 0;
}

static int ab8500_codec_suspend(struct snd_soc_codec *codec,
		pm_message_t state)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab8500_codec_resume(struct snd_soc_codec *codec)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

struct snd_soc_codec_driver ab8500_codec_drv = {
	.probe =		ab8500_codec_probe,
	.remove =		ab8500_codec_remove,
	.suspend =		ab8500_codec_suspend,
	.resume =		ab8500_codec_resume,
	.read =			ab8500_audio_read_reg,
	.write =		ab8500_audio_write_reg,
	.set_bias_level =	ab8500_set_bias_level,
	.reg_cache_size =	ARRAY_SIZE(ab8500_reg_cache),
	.reg_word_size =	sizeof(u8),
	.reg_cache_default =	ab8500_reg_cache,
};

static int __devinit ab8500_codec_drv_probe(struct platform_device *pdev)
{
	int err;

	pr_debug("%s: Enter.\n", __func__);

	err = snd_soc_register_codec(&pdev->dev,
				&ab8500_codec_drv,
				ab8500_codec_dai,
				ARRAY_SIZE(ab8500_codec_dai));

	if (err < 0) {
		pr_err("%s: Error: Failed to register codec (%d).\n",
			__func__, err);
	}

	return err;
}

static int __devexit ab8500_codec_drv_remove(struct platform_device *pdev)
{
	pr_debug("%s Enter.\n", __func__);

	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static int ab8500_codec_drv_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab8500_codec_drv_resume(struct platform_device *pdev)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static struct platform_driver ab8500_codec_platform_drv = {
	.driver	= {
		.name	= "ab8500-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= ab8500_codec_drv_probe,
	.remove		= __devexit_p(ab8500_codec_drv_remove),
	.suspend	= ab8500_codec_drv_suspend,
	.resume		= ab8500_codec_drv_resume,
};

static int __devinit ab8500_codec_platform_drv_init(void)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	ret = platform_driver_register(&ab8500_codec_platform_drv);
	if (ret != 0) {
		pr_err("%s: Failed to register AB8500 platform driver (%d)!\n",
			__func__, ret);
	}

	return ret;
}

static void __exit ab8500_codec_platform_drv_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	platform_driver_unregister(&ab8500_codec_platform_drv);
}

module_init(ab8500_codec_platform_drv_init);
module_exit(ab8500_codec_platform_drv_exit);

MODULE_DESCRIPTION("AB8500 Codec driver");
MODULE_ALIAS("platform:ab8500-codec");
MODULE_AUTHOR("ST-Ericsson");
MODULE_LICENSE("GPL v2");