/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License, version 2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <mach/db5500-keypad.h>

#define KEYPAD_CTR		0x0
#define KEYPAD_IRQ_CLEAR	0x4
#define KEYPAD_INT_ENABLE	0x8
#define KEYPAD_INT_STATUS	0xC
#define KEYPAD_ARRAY_01		0x18

#define KEYPAD_NUM_ARRAY_REGS	5

#define KEYPAD_CTR_WRITE_IRQ_ENABLE	(1 << 10)
#define KEYPAD_CTR_SCAN_ENABLE		(1 << 7)

#define KEYPAD_ARRAY_CHANGEBIT		(1 << 15)

#define KEYPAD_DEBOUNCE_PERIOD_MIN	5	/* ms */
#define KEYPAD_DEBOUNCE_PERIOD_MAX	80	/* ms */

#define KEYPAD_GND_ROW		8

#define KEYPAD_MAX_ROWS		9
#define KEYPAD_MAX_COLS		8
#define KEYPAD_ROW_SHIFT	4
#define KEYPAD_KEYMAP_SIZE	\
	(KEYPAD_MAX_ROWS * KEYPAD_MAX_COLS)

/**
 * struct db5500_keypad  - data structure used by keypad driver
 * @irq:	irq number
 * @base:	keypad registers base address
 * @input:	pointer to input device object
 * @board:	keypad platform data
 * @keymap:	matrix scan code table for keycodes
 * @clk:	clock structure pointer
 * @previous_set: previous set of registers
 */
struct db5500_keypad {
	int irq;
	void __iomem *base;
	struct input_dev *input;
	const struct db5500_keypad_platform_data *board;
	unsigned short keymap[KEYPAD_KEYMAP_SIZE];
	struct clk *clk;
	u8 previous_set[KEYPAD_MAX_ROWS];
};

/*
 * By default all column reads are 1111 1111b.  Any press will pull the column
 * down, leading to a 0 in any of these locations.  We invert these values so
 * that a 1 means means "column pressed".
 *
 * If curr changes from the previous from 0 to 1, we report it as a key press.
 * If curr changes from the previous from 1 to 0, we report it as a key
 * release.
 */
static void db5500_keypad_report(struct db5500_keypad *keypad, int row,
				 u8 curr, u8 previous)
{
	struct input_dev *input = keypad->input;
	u8 changed  = curr ^ previous;

	while (changed) {
		int col = __ffs(changed);
		bool press = curr & BIT(col);
		int code = MATRIX_SCAN_CODE(row, col, KEYPAD_ROW_SHIFT);

		input_event(input, EV_MSC, MSC_SCAN, code);
		input_report_key(input, keypad->keymap[code], press);
		input_sync(input);

		changed &= ~BIT(col);
	}
}

static irqreturn_t db5500_keypad_irq(int irq, void *dev_id)
{
	struct db5500_keypad *keypad = dev_id;
	u8 current_set[ARRAY_SIZE(keypad->previous_set)];
	int tries = 100;
	bool changebit;
	u32 data_reg;
	u8 allrows;
	u8 common;
	int i;

	writel(0x1, keypad->base + KEYPAD_IRQ_CLEAR);

again:
	if (!tries--) {
		dev_warn(&keypad->input->dev, "values failed to stabilize\n");
		return IRQ_HANDLED;
	}

	changebit = readl(keypad->base + KEYPAD_ARRAY_01)
		    & KEYPAD_ARRAY_CHANGEBIT;

	for (i = 0; i < KEYPAD_NUM_ARRAY_REGS; i++) {
		data_reg = readl(keypad->base + KEYPAD_ARRAY_01 + 4 * i);

		/* If the change bit changed, we need to reread the data */
		if (changebit != !!(data_reg & KEYPAD_ARRAY_CHANGEBIT))
			goto again;

		current_set[2 * i] = ~(data_reg & 0xff);

		/* Last array reg has only one valid set of columns */
		if (i != KEYPAD_NUM_ARRAY_REGS - 1)
			current_set[2 * i + 1] = ~((data_reg & 0xff0000) >> 16);
	}

	allrows = current_set[KEYPAD_GND_ROW];

	/*
	 * Sometimes during a GND row release, an incorrect report is received
	 * where the ARRAY8 all rows setting does not match the other ARRAY*
	 * rows.  Ignore this report; the correct one has been observed to
	 * follow it.
	 */
	common = 0xff;
	for (i = 0; i < KEYPAD_GND_ROW; i++)
		common &= current_set[i];

	if ((allrows & common) != common)
		return IRQ_HANDLED;

	for (i = 0; i < ARRAY_SIZE(current_set); i++) {
		/*
		 * If there is an allrows press (GND row), we need to ignore
		 * the allrows values from the reset of the ARRAYs.
		 */
		if (i < KEYPAD_GND_ROW && allrows)
			current_set[i] &= ~allrows;

		if (keypad->previous_set[i] == current_set[i])
			continue;

		db5500_keypad_report(keypad, i, current_set[i],
				     keypad->previous_set[i]);
	}

	/* update the reference set of array registers */
	memcpy(keypad->previous_set, current_set, sizeof(keypad->previous_set));

	return IRQ_HANDLED;
}

static int __devinit db5500_keypad_chip_init(struct db5500_keypad *keypad)
{
	int debounce = keypad->board->debounce_ms;
	int debounce_hits = 0;
	int timeout = 100;
	u32 val;

	if (debounce < KEYPAD_DEBOUNCE_PERIOD_MIN)
		debounce = KEYPAD_DEBOUNCE_PERIOD_MIN;

	if (debounce > KEYPAD_DEBOUNCE_PERIOD_MAX) {
		debounce_hits = DIV_ROUND_UP(debounce,
					     KEYPAD_DEBOUNCE_PERIOD_MAX) - 1;
		debounce = KEYPAD_DEBOUNCE_PERIOD_MAX;
	}

	/* Convert the milliseconds to the bit mask */
	debounce = DIV_ROUND_UP(debounce, KEYPAD_DEBOUNCE_PERIOD_MIN) - 1;

	writel(KEYPAD_CTR_SCAN_ENABLE
		| ((debounce_hits & 0x7) << 4)
		| debounce, keypad->base + KEYPAD_CTR);

	do {
		val = readl(keypad->base + KEYPAD_CTR);
	} while ((!(val & KEYPAD_CTR_WRITE_IRQ_ENABLE)) && --timeout);

	if (!timeout)
		return -EINVAL;

	writel(0x1, keypad->base + KEYPAD_INT_ENABLE);

	return 0;
}

static int __devinit db5500_keypad_probe(struct platform_device *pdev)
{
	const struct db5500_keypad_platform_data *plat;
	struct db5500_keypad *keypad;
	struct resource *res;
	struct input_dev *input;
	void __iomem *base;
	struct clk *clk;
	int ret;
	int irq;

	plat = pdev->dev.platform_data;
	if (!plat) {
		dev_err(&pdev->dev, "invalid keypad platform data\n");
		ret = -EINVAL;
		goto out_ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad irq\n");
		ret = -EINVAL;
		goto out_ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "missing platform resources\n");
		ret = -EINVAL;
		goto out_ret;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		ret = -EBUSY;
		goto out_ret;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		ret = -ENXIO;
		goto out_freerequest_memregions;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to clk_get\n");

		/*
		 * FIXME: error out here once DB5500 clock framework is in
		 * place, and remove all the !IS_ERR(clk) checks.
		 */
	}

	keypad = kzalloc(sizeof(struct db5500_keypad), GFP_KERNEL);
	if (!keypad) {
		dev_err(&pdev->dev, "failed to allocate keypad memory\n");
		ret = -ENOMEM;
		goto out_freeclk;
	}

	input = input_allocate_device();
	if (!input) {
		dev_err(&pdev->dev, "failed to input_allocate_device\n");
		ret = -ENOMEM;
		goto out_freekeypad;
	}

	input->id.bustype = BUS_HOST;
	input->name = "db5500-keypad";
	input->dev.parent = &pdev->dev;

	input->keycode = keypad->keymap;
	input->keycodesize = sizeof(keypad->keymap[0]);
	input->keycodemax = ARRAY_SIZE(keypad->keymap);

	input_set_capability(input, EV_MSC, MSC_SCAN);

	__set_bit(EV_KEY, input->evbit);
	if (!plat->no_autorepeat)
		__set_bit(EV_REP, input->evbit);

	matrix_keypad_build_keymap(plat->keymap_data, KEYPAD_ROW_SHIFT,
				   input->keycode, input->keybit);

	ret = input_register_device(input);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to register input device: %d\n", ret);
		goto out_freeinput;
	}

	keypad->irq	= irq;
	keypad->board	= plat;
	keypad->input	= input;
	keypad->base	= base;
	keypad->clk	= clk;

	/* allocations are sane, we begin HW initialization */
	if (!IS_ERR(keypad->clk))
		clk_enable(keypad->clk);

	ret = db5500_keypad_chip_init(keypad);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to init keypad hardware\n");
		goto out_unregisterinput;
	}

	ret = request_threaded_irq(keypad->irq, NULL, db5500_keypad_irq,
				   IRQF_ONESHOT, "db5500-keypad", keypad);
	if (ret) {
		dev_err(&pdev->dev, "allocate irq %d failed\n", keypad->irq);
		goto out_unregisterinput;
	}

	device_init_wakeup(&pdev->dev, true);

	platform_set_drvdata(pdev, keypad);

	return 0;

out_unregisterinput:
	input_unregister_device(input);
	input = NULL;
	if (!IS_ERR(keypad->clk))
		clk_disable(keypad->clk);
out_freeinput:
	input_free_device(input);
out_freekeypad:
	kfree(keypad);
out_freeclk:
	if (!IS_ERR(clk))
		clk_put(clk);
	iounmap(base);
out_freerequest_memregions:
	release_mem_region(res->start, resource_size(res));
out_ret:
	return ret;
}

static int __devexit db5500_keypad_remove(struct platform_device *pdev)
{
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	free_irq(keypad->irq, keypad);
	input_unregister_device(keypad->input);

	if (!IS_ERR(keypad->clk)) {
		clk_disable(keypad->clk);
		clk_put(keypad->clk);
	}

	iounmap(keypad->base);
	release_mem_region(res->start, resource_size(res));
	kfree(keypad);

	return 0;
}

#ifdef CONFIG_PM
static int db5500_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		enable_irq_wake(irq);
	else
		/* disable IRQ here */

	return 0;
}

static int db5500_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		disable_irq_wake(irq);
	else
		/* enable IRQ here */

	return 0;
}

static const struct dev_pm_ops db5500_keypad_dev_pm_ops = {
	.suspend = db5500_keypad_suspend,
	.resume = db5500_keypad_resume,
};
#endif

static struct platform_driver db5500_keypad_driver = {
	.driver = {
		.name	= "db5500-keypad",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &db5500_keypad_dev_pm_ops,
#endif
	},
	.probe	= db5500_keypad_probe,
	.remove	= __devexit_p(db5500_keypad_remove),
};

static int __init db5500_keypad_init(void)
{
	return platform_driver_register(&db5500_keypad_driver);
}
module_init(db5500_keypad_init);

static void __exit db5500_keypad_exit(void)
{
	platform_driver_unregister(&db5500_keypad_driver);
}
module_exit(db5500_keypad_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_DESCRIPTION("DB5500 Keypad Driver");
MODULE_ALIAS("platform:db5500-keypad");