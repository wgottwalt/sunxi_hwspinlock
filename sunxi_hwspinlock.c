// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sunxi_hwspinlock.c - hardware spinlock driver for Allwinner SoCs
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "hwspinlock_internal.h"

#define DRIVER_NAME		"sunxi_hwspinlock"

#define SPINLOCK_BASE_ID	0 /* there is only one hwspinlock device per SoC */
#define SPINLOCK_SYSSTATUS_REG	0x0000
#define SPINLOCK_LOCK_REGN	0x0100
#define SPINLOCK_NOTTAKEN	0

struct sunxi_hwspinlock_data {
	struct hwspinlock_device *bank;
	struct reset_control *reset;
	struct clk *ahb_clock;
	struct dentry *debugfs;
	int nlocks;
};

#ifdef CONFIG_DEBUG_FS

static int hwlocks_supported_show(struct seq_file *seqf, void *unused)
{
	struct sunxi_hwspinlock_data *priv = seqf->private;

	seq_printf(seqf, "%d\n", priv->nlocks);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hwlocks_supported);

static void sunxi_hwspinlock_debugfs_init(struct sunxi_hwspinlock_data *priv)
{
	priv->debugfs = debugfs_create_dir(DRIVER_NAME, NULL);
	debugfs_create_file("supported", 0444, priv->debugfs, priv, &hwlocks_supported_fops);
}

#else

static void sunxi_hwspinlock_debugfs_init(struct sunxi_hwspinlock_data *priv)
{
}

#endif

static int sunxi_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	return (readl(lock_addr) == SPINLOCK_NOTTAKEN);
}

static void sunxi_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(SPINLOCK_NOTTAKEN, lock_addr);
}

static const struct hwspinlock_ops sunxi_hwspinlock_ops = {
	.trylock	= sunxi_hwspinlock_trylock,
	.unlock		= sunxi_hwspinlock_unlock,
};

static int sunxi_hwspinlock_probe(struct platform_device *pdev)
{
	struct sunxi_hwspinlock_data *priv;
	struct hwspinlock *hwlock;
	void __iomem *io_base;
	int num_banks, err, i;

	io_base = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID);
	if (IS_ERR(io_base)) {
		err = PTR_ERR(io_base);
		dev_err(&pdev->dev, "unable to request MMIO (%d)\n", err);
		return err;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ahb_clock = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->ahb_clock)) {
		err = PTR_ERR(priv->ahb_clock);
		dev_err(&pdev->dev, "unable to get AHB clock (%d)\n", err);
		return err;
	}

	priv->reset = devm_reset_control_get_optional(&pdev->dev, "ahb");
	if (IS_ERR(priv->reset)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->reset),
				     "unable to get reset control\n");
	}

	err = reset_control_deassert(priv->reset);
	if (err) {
		dev_err(&pdev->dev, "deassert reset control failure (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(priv->ahb_clock);
	if (err) {
		dev_err(&pdev->dev, "unable to prepare AHB clock (%d)\n", err);
		goto reset_fail;
	}

	/*
	 * bit 28 and 29 hold the amount of spinlock banks, but at the same time the datasheet
	 * says, bit 30 and 31 are reserved while the values can be 0 to 4, which is not reachable
	 * by two bits alone, so the reserved bits are also taken into account
	 *
	 * order is important here, getting the amount of locks is only possible after setting up
	 * clocks and resets
	 */
	num_banks = readl(io_base + SPINLOCK_SYSSTATUS_REG) >> 28;
	switch (num_banks) {
	case 1 ... 4:
		/*
		 * 32, 64, 128 and 256 spinlocks are supported by the hardware implementation,
		 * though most of the SoCs support 32 spinlocks only
		 */
		priv->nlocks = 1 << (5 + i);
		break;
	default:
		dev_err(&pdev->dev, "unsupported hwspinlock setup (%d)\n", num_banks);
		return -EINVAL;
	}

	priv->bank = devm_kzalloc(&pdev->dev, struct_size(priv->bank, lock, priv->nlocks),
				  GFP_KERNEL);
	if (!priv->bank) {
		err = -ENOMEM;
		goto reset_fail;
	}

	for (i = 0; i < priv->nlocks; ++i) {
		hwlock = &priv->bank->lock[i];
		hwlock->priv = io_base + SPINLOCK_LOCK_REGN + sizeof(u32) * i;
	}

	err = hwspin_lock_register(priv->bank, &pdev->dev, &sunxi_hwspinlock_ops, SPINLOCK_BASE_ID,
				   priv->nlocks);
	if (err) {
		dev_err(&pdev->dev, "unable to register hwspinlocks (%d)\n", err);
		goto fail;
	}

	sunxi_hwspinlock_debugfs_init(priv);
	platform_set_drvdata(pdev, priv);

	dev_dbg(&pdev->dev, "SUNXI hardware spinlock driver enabled (%d locks)\n", priv->nlocks);

	return 0;

fail:
	clk_disable_unprepare(priv->ahb_clock);

reset_fail:
	if (priv->reset)
		reset_control_assert(priv->reset);

	return err;
}

static int sunxi_hwspinlock_remove(struct platform_device *pdev)
{
	struct sunxi_hwspinlock_data *priv = platform_get_drvdata(pdev);
	int err;

	debugfs_remove_recursive(priv->debugfs);

	err = hwspin_lock_unregister(priv->bank);
	if (err) {
		dev_err(&pdev->dev, "unregister device failed (%d)\n", err);
		return err;
	}

	if (priv->reset)
		reset_control_assert(priv->reset);

	clk_disable_unprepare(priv->ahb_clock);

	return 0;
}

static const struct of_device_id sunxi_hwspinlock_ids[] = {
	{ .compatible = "allwinner,sun8i-hwspinlock", },
	{ .compatible = "allwinner,sun50i-hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_hwspinlock_ids);

static struct platform_driver sunxi_hwspinlock_driver = {
	.probe	= sunxi_hwspinlock_probe,
	.remove	= sunxi_hwspinlock_remove,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= sunxi_hwspinlock_ids,
	},
};

static int __init sunxi_hwspinlock_init(void)
{
	return platform_driver_register(&sunxi_hwspinlock_driver);
}
postcore_initcall(sunxi_hwspinlock_init);

static void __exit sunxi_hwspinlock_exit(void)
{
	platform_driver_unregister(&sunxi_hwspinlock_driver);
}
module_exit(sunxi_hwspinlock_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SUNXI hardware spinlock driver");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
