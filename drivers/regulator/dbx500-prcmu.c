/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * UX500 common part of Power domain regulators
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "dbx500-prcmu.h"

/*
 * power state reference count
 */
static int power_state_active_cnt; /* will initialize to zero */
static DEFINE_SPINLOCK(power_state_active_lock);

void power_state_active_enable(void)
{
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	power_state_active_cnt++;
	spin_unlock_irqrestore(&power_state_active_lock, flags);
}

int power_state_active_disable(void)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	if (power_state_active_cnt <= 0) {
		pr_err("power state: unbalanced enable/disable calls\n");
		ret = -EINVAL;
		goto out;
	}

	power_state_active_cnt--;
out:
	spin_unlock_irqrestore(&power_state_active_lock, flags);
	return ret;
}

/*
 * Exported interface for CPUIdle only. This function is called when interrupts
 * are turned off. Hence, no locking.
 */
int power_state_active_is_enabled(void)
{
	return (power_state_active_cnt > 0);
}

struct ux500_regulator {
	char *name;
	void (*enable)(void);
	int (*disable)(void);
};
static struct ux500_regulator ux500_atomic_regulators[] = {
	{
		.name    = "dma40.0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "ssp0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "ssp1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi0",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi2",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "spi3",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "cryp1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
	{
		.name = "hash1",
		.enable  = power_state_active_enable,
		.disable = power_state_active_disable,
	},
};

struct ux500_regulator *__must_check ux500_regulator_get(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ux500_atomic_regulators); i++) {
		if (!strcmp(dev_name(dev), ux500_atomic_regulators[i].name))
			return &ux500_atomic_regulators[i];
	}

	return  ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(ux500_regulator_get);

int ux500_regulator_atomic_enable(struct ux500_regulator *regulator)
{
	if (regulator) {
		regulator->enable();
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ux500_regulator_atomic_enable);

int ux500_regulator_atomic_disable(struct ux500_regulator *regulator)
{
	if (regulator)
		return regulator->disable();
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(ux500_regulator_atomic_disable);

void ux500_regulator_put(struct ux500_regulator *regulator)
{
	/* Here for symetric reasons and for possible future use */
}
EXPORT_SYMBOL_GPL(ux500_regulator_put);

#ifdef CONFIG_REGULATOR_DEBUG

static struct ux500_regulator_debug {
	struct dentry *dir;
	struct dentry *status_file;
	struct dbx500_regulator_info *regulator_array;
	int num_regulators;
	u8 *state_before_suspend;
	u8 *state_after_suspend;
} rdebug;

void ux500_regulator_suspend_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_before_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

void ux500_regulator_resume_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_after_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

static int ux500_regulator_status_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int err;
	int i;

	/* print dump header */
	err = seq_printf(s, "ux500-regulator status:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	err = seq_printf(s, "%31s : %8s : %8s\n", "current",
		"before", "after");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	for (i = 0; i < rdebug.num_regulators; i++) {
		struct dbx500_regulator_info *info;
		/* Access per-regulator data */
		info = &rdebug.regulator_array[i];

		/* print status */
		err = seq_printf(s, "%20s : %8s : %8s : %8s\n", info->desc.name,
			info->is_enabled ? "enabled" : "disabled",
			rdebug.state_before_suspend[i] ? "enabled" : "disabled",
			rdebug.state_after_suspend[i] ? "enabled" : "disabled");
		if (err < 0)
			dev_err(dev, "seq_printf overflow\n");
	}

	return 0;
}

static int ux500_regulator_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ux500_regulator_status_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_status_fops = {
	.open = ux500_regulator_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

int __devinit
ux500_regulator_debug_init(struct platform_device *pdev,
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	/* create directory */
	rdebug.dir = debugfs_create_dir("ux500-regulator", NULL);
	if (!rdebug.dir)
		goto exit_no_debugfs;

	/* create "status" file */
	rdebug.status_file = debugfs_create_file("status",
		S_IRUGO, rdebug.dir, &pdev->dev,
		&ux500_regulator_status_fops);
	if (!rdebug.status_file)
		goto exit_destroy_dir;

	rdebug.regulator_array = regulator_info;
	rdebug.num_regulators = num_regulators;

	rdebug.state_before_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_before_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_destory_status;
	}

	rdebug.state_after_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_after_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_free;
	}
	return 0;

exit_free:
	kfree(rdebug.state_before_suspend);
exit_destory_status:
	debugfs_remove(rdebug.status_file);
exit_destroy_dir:
	debugfs_remove(rdebug.dir);
exit_no_debugfs:
	dev_err(&pdev->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

int __devexit ux500_regulator_debug_exit(void)
{
	debugfs_remove_recursive(rdebug.dir);
	kfree(rdebug.state_after_suspend);
	kfree(rdebug.state_before_suspend);

	return 0;
}
#endif
