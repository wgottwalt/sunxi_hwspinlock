// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sun6i_hwspinlock_mod.c - hardware spinlock driver for sun6i compatible Allwinner SoCs
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

#define DRIVER_NAME		"sun6i_hwspinlock_mod"

#define SPINLOCK_BASE_ID	0 /* there is only one hwspinlock device per SoC */
#define SPINLOCK_SYSSTATUS_REG	0x0000
#define SPINLOCK_NOTTAKEN	0

struct sun6i_hwspinlock_mod_data {
	struct hwspinlock_device *bank;
	struct reset_control *reset;
	struct clk *ahb_clk;
	struct dentry *debugfs;
	int nlocks;
};

#ifdef CONFIG_DEBUG_FS

static int hwlocks_supported_show(struct seq_file *seqf, void *unused)
{
	struct sun6i_hwspinlock_mod_data *priv = seqf->private;

	seq_printf(seqf, "%d\n", priv->nlocks);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hwlocks_supported);

static void sun6i_hwspinlock_mod_debugfs_init(struct sun6i_hwspinlock_mod_data *priv)
{
	priv->debugfs = debugfs_create_dir(DRIVER_NAME, NULL);
	debugfs_create_file("supported", 0444, priv->debugfs, priv, &hwlocks_supported_fops);
}

#else

static void sun6i_hwspinlock_mod_debugfs_init(struct sun6i_hwspinlock_mod_data *priv)
{
}

#endif

static int sun6i_hwspinlock_mod_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	return (readl(lock_addr) == SPINLOCK_NOTTAKEN);
}

static void sun6i_hwspinlock_mod_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(SPINLOCK_NOTTAKEN, lock_addr);
}

static const struct hwspinlock_ops sun6i_hwspinlock_mod_ops = {
	.trylock	= sun6i_hwspinlock_mod_trylock,
	.unlock		= sun6i_hwspinlock_mod_unlock,
};

static int sun6i_hwspinlock_mod_probe(struct platform_device *pdev)
{
	struct sun6i_hwspinlock_mod_data *priv;
	struct hwspinlock *hwlock;
	void __iomem *io_base;
	void __iomem *io_locks;
	u32 num_banks;
	int err, i;

	io_base = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	io_locks = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID + 1);
	if (IS_ERR(io_locks))
		return PTR_ERR(io_locks);

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->ahb_clk)) {
		err = PTR_ERR(priv->ahb_clk);
		dev_err(&pdev->dev, "unable to get AHB clock (%d)\n", err);
		return err;
	}

	priv->reset = devm_reset_control_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->reset),
				     "unable to get reset control\n");

	err = reset_control_deassert(priv->reset);
	if (err) {
		dev_err(&pdev->dev, "deassert reset control failure (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(priv->ahb_clk);
	if (err) {
		dev_err(&pdev->dev, "unable to prepare AHB clk (%d)\n", err);
		goto clk_fail;
	}

	/*
	 * bit 28 and 29 represents the hwspinlock setup
	 *
	 * every datasheet (A64, A80, A83T, H3, H5, H6 ...) says the default value is 0x1 and 0x1
	 * to 0x4 represent 32, 64, 128 and 256 locks
	 * but later datasheets (H5, H6) say 00, 01, 10, 11 represent 32, 64, 128 and 256 locks,
	 * but that would mean H5 and H6 have 64 locks, while their datasheets talk about 32 locks
	 * all the time, not a single mentioning of 64 locks
	 * the 0x4 value is also not representable by 2 bits alone, so some datasheets are not
	 * correct
	 * one thing have all in common, default value of the sysstatus register is 0x10000000,
	 * which results in bit 28 being set
	 * this is the reason 0x1 is considered being 32 locks and bit 30 is taken into account
	 * verified on H2+ (datasheet 0x1 = 32 locks) and H5 (datasheet 01 = 64 locks)
	 */
	num_banks = readl(io_base + SPINLOCK_SYSSTATUS_REG) >> 28;
	switch (num_banks) {
	case 1 ... 4:
		priv->nlocks = 1 << (4 + num_banks);
		break;
	default:
		err = -EINVAL;
		dev_err(&pdev->dev, "unsupported hwspinlock setup (%d)\n", num_banks);
		goto bank_fail;
	}

	priv->bank = devm_kzalloc(&pdev->dev, struct_size(priv->bank, lock, priv->nlocks),
				  GFP_KERNEL);
	if (!priv->bank) {
		err = -ENOMEM;
		goto bank_fail;
	}

	for (i = 0; i < priv->nlocks; ++i) {
		hwlock = &priv->bank->lock[i];
		hwlock->priv = io_locks + sizeof(u32) * i;
	}

	/* failure of debugfs is considered non-fatal */
	sun6i_hwspinlock_mod_debugfs_init(priv);
	if (IS_ERR(priv->debugfs))
		priv->debugfs = NULL;

	platform_set_drvdata(pdev, priv);

	err = hwspin_lock_register(priv->bank, &pdev->dev, &sun6i_hwspinlock_mod_ops,
				   SPINLOCK_BASE_ID, priv->nlocks);

	if (err == 0)
		return 0;

bank_fail:
	clk_disable_unprepare(priv->ahb_clk);
clk_fail:
	reset_control_assert(priv->reset);

	return err;
}

static int sun6i_hwspinlock_mod_remove(struct platform_device *pdev)
{
	struct sun6i_hwspinlock_mod_data *priv = platform_get_drvdata(pdev);
	int err;

	debugfs_remove_recursive(priv->debugfs);

	err = hwspin_lock_unregister(priv->bank);
	if (err) {
		dev_err(&pdev->dev, "unregister device failed (%d)\n", err);
		return err;
	}

	clk_disable_unprepare(priv->ahb_clk);
	reset_control_assert(priv->reset);

	return 0;
}

static const struct of_device_id sun6i_hwspinlock_mod_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-hwspinlock-mod", },
	{},
};
MODULE_DEVICE_TABLE(of, sun6i_hwspinlock_mod_ids);

static struct platform_driver sun6i_hwspinlock_mod_driver = {
	.probe	= sun6i_hwspinlock_mod_probe,
	.remove	= sun6i_hwspinlock_mod_remove,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= sun6i_hwspinlock_mod_ids,
	},
};
module_platform_driver(sun6i_hwspinlock_mod_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SUN6I hardware spinlock enhanced driver");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
